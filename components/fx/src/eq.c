/**
 * @file eq.c
 * @brief 3-Band EQ implementation with biquad filters and kill switches
 * 
 * Optimized for ESP32-S3 with:
 * - Linkwitz-Riley crossover filters for clean band separation
 * - Static scratch buffer (avoids stack overflow)
 * - IRAM placement for hot processing loop
 * - Polynomial soft clipper for tube-like saturation
 */

#include "eq.h"
#include "esp_log.h"
#include "esp_attr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "eq";

// Crossover frequencies
#define EQ_LOW_CROSSOVER_HZ   250.0f
#define EQ_HIGH_CROSSOVER_HZ  4000.0f

// Static scratch buffers for float conversion and band separation
// Avoids stack overflow on ESP32
// 256 stereo samples = 512 floats per buffer, need 4 buffers
static __attribute__((aligned(16))) float scratch_input[512];
static __attribute__((aligned(16))) float scratch_low[512];
static __attribute__((aligned(16))) float scratch_mid[512];
static __attribute__((aligned(16))) float scratch_high[512];

// ============================================================================
// Biquad Filter Coefficient Calculation
// ============================================================================

/**
 * @brief Calculate lowpass biquad coefficients (2nd order Butterworth)
 */
static void calc_lowpass_coeffs(eq_biquad_t *bq, float fc, float fs) {
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * 0.7071f);  // Q = 0.7071 for Butterworth
    
    float a0 = 1.0f + alpha;
    bq->b0 = ((1.0f - cos_w0) / 2.0f) / a0;
    bq->b1 = (1.0f - cos_w0) / a0;
    bq->b2 = ((1.0f - cos_w0) / 2.0f) / a0;
    bq->a1 = (-2.0f * cos_w0) / a0;
    bq->a2 = (1.0f - alpha) / a0;
}

/**
 * @brief Calculate highpass biquad coefficients (2nd order Butterworth)
 */
static void calc_highpass_coeffs(eq_biquad_t *bq, float fc, float fs) {
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * 0.7071f);  // Q = 0.7071 for Butterworth
    
    float a0 = 1.0f + alpha;
    bq->b0 = ((1.0f + cos_w0) / 2.0f) / a0;
    bq->b1 = (-(1.0f + cos_w0)) / a0;
    bq->b2 = ((1.0f + cos_w0) / 2.0f) / a0;
    bq->a1 = (-2.0f * cos_w0) / a0;
    bq->a2 = (1.0f - alpha) / a0;
}

/**
 * @brief Reset biquad delay line state
 */
static void biquad_reset(eq_biquad_t *bq) {
    bq->z1 = 0.0f;
    bq->z2 = 0.0f;
}

/**
 * @brief Initialize biquad to unity/bypass (all-pass with gain 1)
 */
static void biquad_init_bypass(eq_biquad_t *bq) {
    bq->b0 = 1.0f;
    bq->b1 = 0.0f;
    bq->b2 = 0.0f;
    bq->a1 = 0.0f;
    bq->a2 = 0.0f;
    bq->z1 = 0.0f;
    bq->z2 = 0.0f;
}

// ============================================================================
// Biquad Processing (Direct Form II Transposed)
// ============================================================================

/**
 * @brief Process a single sample through biquad filter (Direct Form II Transposed)
 * 
 * This form has better numerical properties for fixed-point and float.
 */
static inline float IRAM_ATTR biquad_process_sample(eq_biquad_t *bq, float in) {
    float out = bq->b0 * in + bq->z1;
    bq->z1 = bq->b1 * in - bq->a1 * out + bq->z2;
    bq->z2 = bq->b2 * in - bq->a2 * out;
    return out;
}

/**
 * @brief Process buffer through biquad filter in-place
 */
static void IRAM_ATTR biquad_process_buffer(eq_biquad_t *bq, float *buffer, size_t count) {
    for (size_t i = 0; i < count; i++) {
        buffer[i] = biquad_process_sample(bq, buffer[i]);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert dB to linear gain
 */
static inline float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

/**
 * @brief Polynomial soft clipper: f(x) = x - x³/3
 * 
 * Provides tube-like saturation without harsh harmonics.
 */
static inline float IRAM_ATTR soft_clip(float x) {
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) / 3.0f;
}

// ============================================================================
// Public API Implementation
// ============================================================================

void eq_3band_init(eq_3band_t *eq, uint32_t sample_rate) {
    if (!eq) return;
    
    memset(eq, 0, sizeof(eq_3band_t));
    eq->sample_rate = sample_rate;
    eq->enabled = true;
    
    // Initialize all gains to unity (1.0 linear = 0dB)
    eq->gain_low = 1.0f;
    eq->gain_mid = 1.0f;
    eq->gain_high = 1.0f;
    
    // All kill switches off
    eq->kill_low = false;
    eq->kill_mid = false;
    eq->kill_high = false;
    
    // Initialize band EQ filters as bypass
    biquad_init_bypass(&eq->low_l);
    biquad_init_bypass(&eq->low_r);
    biquad_init_bypass(&eq->mid_l);
    biquad_init_bypass(&eq->mid_r);
    biquad_init_bypass(&eq->high_l);
    biquad_init_bypass(&eq->high_r);
    
    // Calculate crossover filter coefficients
    eq_3band_recalculate_coeffs(eq);
    
    ESP_LOGI(TAG, "3-band EQ initialized @ %lu Hz (crossover: %d/%d Hz)",
             (unsigned long)sample_rate,
             (int)EQ_LOW_CROSSOVER_HZ, (int)EQ_HIGH_CROSSOVER_HZ);
}

void eq_3band_recalculate_coeffs(eq_3band_t *eq) {
    if (!eq || eq->sample_rate == 0) return;
    
    float fs = (float)eq->sample_rate;
    
    // Low crossover (250Hz)
    // Lowpass for low band extraction
    calc_lowpass_coeffs(&eq->xover_low_lp_l, EQ_LOW_CROSSOVER_HZ, fs);
    calc_lowpass_coeffs(&eq->xover_low_lp_r, EQ_LOW_CROSSOVER_HZ, fs);
    
    // Highpass for mid/high separation from low
    calc_highpass_coeffs(&eq->xover_low_hp_l, EQ_LOW_CROSSOVER_HZ, fs);
    calc_highpass_coeffs(&eq->xover_low_hp_r, EQ_LOW_CROSSOVER_HZ, fs);
    
    // High crossover (4kHz)
    // Lowpass for mid band (combined with low HP = bandpass 250-4k)
    calc_lowpass_coeffs(&eq->xover_high_lp_l, EQ_HIGH_CROSSOVER_HZ, fs);
    calc_lowpass_coeffs(&eq->xover_high_lp_r, EQ_HIGH_CROSSOVER_HZ, fs);
    
    // Highpass for high band extraction
    calc_highpass_coeffs(&eq->xover_high_hp_l, EQ_HIGH_CROSSOVER_HZ, fs);
    calc_highpass_coeffs(&eq->xover_high_hp_r, EQ_HIGH_CROSSOVER_HZ, fs);
}

void eq_3band_set_low_gain(eq_3band_t *eq, float gain_db) {
    if (!eq) return;
    gain_db = fmaxf(-24.0f, fminf(12.0f, gain_db));
    eq->gain_low = db_to_linear(gain_db);
}

void eq_3band_set_mid_gain(eq_3band_t *eq, float gain_db) {
    if (!eq) return;
    gain_db = fmaxf(-24.0f, fminf(12.0f, gain_db));
    eq->gain_mid = db_to_linear(gain_db);
}

void eq_3band_set_high_gain(eq_3band_t *eq, float gain_db) {
    if (!eq) return;
    gain_db = fmaxf(-24.0f, fminf(12.0f, gain_db));
    eq->gain_high = db_to_linear(gain_db);
}

void eq_3band_set_gains(eq_3band_t *eq, float low_db, float mid_db, float high_db) {
    eq_3band_set_low_gain(eq, low_db);
    eq_3band_set_mid_gain(eq, mid_db);
    eq_3band_set_high_gain(eq, high_db);
}

void eq_3band_set_low_kill(eq_3band_t *eq, bool kill) {
    if (!eq) return;
    eq->kill_low = kill;
    ESP_LOGD(TAG, "Low kill: %s", kill ? "ON" : "OFF");
}

void eq_3band_set_mid_kill(eq_3band_t *eq, bool kill) {
    if (!eq) return;
    eq->kill_mid = kill;
    ESP_LOGD(TAG, "Mid kill: %s", kill ? "ON" : "OFF");
}

void eq_3band_set_high_kill(eq_3band_t *eq, bool kill) {
    if (!eq) return;
    eq->kill_high = kill;
    ESP_LOGD(TAG, "High kill: %s", kill ? "ON" : "OFF");
}

bool eq_3band_toggle_low_kill(eq_3band_t *eq) {
    if (!eq) return false;
    eq->kill_low = !eq->kill_low;
    ESP_LOGD(TAG, "Low kill toggled: %s", eq->kill_low ? "ON" : "OFF");
    return eq->kill_low;
}

bool eq_3band_toggle_mid_kill(eq_3band_t *eq) {
    if (!eq) return false;
    eq->kill_mid = !eq->kill_mid;
    ESP_LOGD(TAG, "Mid kill toggled: %s", eq->kill_mid ? "ON" : "OFF");
    return eq->kill_mid;
}

bool eq_3band_toggle_high_kill(eq_3band_t *eq) {
    if (!eq) return false;
    eq->kill_high = !eq->kill_high;
    ESP_LOGD(TAG, "High kill toggled: %s", eq->kill_high ? "ON" : "OFF");
    return eq->kill_high;
}

void eq_3band_set_enabled(eq_3band_t *eq, bool enabled) {
    if (!eq) return;
    eq->enabled = enabled;
    ESP_LOGD(TAG, "EQ %s", enabled ? "enabled" : "bypassed");
}

void eq_3band_reset(eq_3band_t *eq) {
    if (!eq) return;
    
    // Reset all biquad delay lines
    biquad_reset(&eq->low_l);
    biquad_reset(&eq->low_r);
    biquad_reset(&eq->mid_l);
    biquad_reset(&eq->mid_r);
    biquad_reset(&eq->high_l);
    biquad_reset(&eq->high_r);
    
    biquad_reset(&eq->xover_low_lp_l);
    biquad_reset(&eq->xover_low_lp_r);
    biquad_reset(&eq->xover_low_hp_l);
    biquad_reset(&eq->xover_low_hp_r);
    biquad_reset(&eq->xover_high_lp_l);
    biquad_reset(&eq->xover_high_lp_r);
    biquad_reset(&eq->xover_high_hp_l);
    biquad_reset(&eq->xover_high_hp_r);
    
    ESP_LOGD(TAG, "EQ state reset");
}

void IRAM_ATTR eq_3band_process(eq_3band_t *eq, int16_t *buffer, size_t samples) {
    // Early exit if disabled or null
    if (!eq || !eq->enabled || !buffer || samples == 0) {
        return;
    }
    
    // Clamp samples to scratch buffer size (256 stereo frames)
    if (samples > 256) {
        samples = 256;
    }
    
    const float scale_in = 1.0f / 32768.0f;
    const float scale_out = 32767.0f;
    
    // 1. Convert INT16 -> FLOAT (normalized to -1.0 to 1.0)
    for (size_t i = 0; i < samples * 2; i++) {
        scratch_input[i] = (float)buffer[i] * scale_in;
    }
    
    // 2. Split into bands using crossover filters
    // Process left and right channels separately
    
    for (size_t i = 0; i < samples; i++) {
        float l_in = scratch_input[i * 2];
        float r_in = scratch_input[i * 2 + 1];
        
        // === LOW BAND (< 250Hz) ===
        // Lowpass at 250Hz
        float l_low = biquad_process_sample(&eq->xover_low_lp_l, l_in);
        float r_low = biquad_process_sample(&eq->xover_low_lp_r, r_in);
        
        // === REMAINING (> 250Hz) ===
        // Highpass at 250Hz
        float l_rest = biquad_process_sample(&eq->xover_low_hp_l, l_in);
        float r_rest = biquad_process_sample(&eq->xover_low_hp_r, r_in);
        
        // === MID BAND (250Hz - 4kHz) ===
        // Lowpass the remainder at 4kHz
        float l_mid = biquad_process_sample(&eq->xover_high_lp_l, l_rest);
        float r_mid = biquad_process_sample(&eq->xover_high_lp_r, r_rest);
        
        // === HIGH BAND (> 4kHz) ===
        // Highpass the remainder at 4kHz
        float l_high = biquad_process_sample(&eq->xover_high_hp_l, l_rest);
        float r_high = biquad_process_sample(&eq->xover_high_hp_r, r_rest);
        
        // Store separated bands
        scratch_low[i * 2] = l_low;
        scratch_low[i * 2 + 1] = r_low;
        scratch_mid[i * 2] = l_mid;
        scratch_mid[i * 2 + 1] = r_mid;
        scratch_high[i * 2] = l_high;
        scratch_high[i * 2 + 1] = r_high;
    }
    
    // 3. Apply gains and kill switches, then recombine
    float gain_low = eq->kill_low ? 0.0f : eq->gain_low;
    float gain_mid = eq->kill_mid ? 0.0f : eq->gain_mid;
    float gain_high = eq->kill_high ? 0.0f : eq->gain_high;
    
    for (size_t i = 0; i < samples * 2; i++) {
        // Apply band gains
        float low = scratch_low[i] * gain_low;
        float mid = scratch_mid[i] * gain_mid;
        float high = scratch_high[i] * gain_high;
        
        // Sum bands
        float out = low + mid + high;
        
        // Apply soft limiter
        out = soft_clip(out);
        
        // Convert back to INT16
        float val = out * scale_out;
        if (val > 32767.0f) val = 32767.0f;
        if (val < -32768.0f) val = -32768.0f;
        buffer[i] = (int16_t)val;
    }
}
