/**
 * @file filter.c
 * @brief Resonant filter and DJ EQ implementation
 * 
 * Optimized for ESP32-S3 with:
 * - Biquad IIR resonant HPF/LPF (transposed direct form II)
 * - Static scratch buffer (avoids stack overflow)
 * - IRAM placement for hot processing loops
 * - Polynomial soft clipper for tube-like saturation
 */

#include "filter.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "filter";

// Constants
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define FILTER_CUTOFF_MIN   20.0f
#define FILTER_CUTOFF_MAX   20000.0f
#define FILTER_Q_MIN        0.5f
#define FILTER_Q_MAX        20.0f

// Static scratch buffer for float conversion (avoids stack overflow)
// 2 channels * 256 samples = 512 floats = 2KB
// Allocated in .bss segment, not on stack
// MUST be 16-byte aligned for potential SIMD operations
static __attribute__((aligned(16))) float scratch_buf[512];

/* ============================================================================
 * Resonant Filter Implementation
 * ============================================================================ */

/**
 * @brief Recalculate biquad coefficients from filter parameters
 * 
 * Uses standard biquad formulas from Audio EQ Cookbook.
 * Transposed direct form II: y = b0*x + w1; w1 = b1*x - a1*y + w2; w2 = b2*x - a2*y
 */
static void resonant_filter_calc_coeffs(resonant_filter_t *filter) {
    if (!filter || filter->sample_rate == 0) return;
    
    // Clamp cutoff to valid range (prevent instability)
    float fc = filter->cutoff_hz;
    if (fc < FILTER_CUTOFF_MIN) fc = FILTER_CUTOFF_MIN;
    if (fc > filter->sample_rate * 0.45f) fc = filter->sample_rate * 0.45f; // Stay below Nyquist
    
    float Q = filter->resonance;
    if (Q < FILTER_Q_MIN) Q = FILTER_Q_MIN;
    if (Q > FILTER_Q_MAX) Q = FILTER_Q_MAX;
    
    // Pre-warp frequency for bilinear transform
    float omega = 2.0f * M_PI * fc / (float)filter->sample_rate;
    float sin_w = sinf(omega);
    float cos_w = cosf(omega);
    float alpha = sin_w / (2.0f * Q);
    
    float a0;
    
    if (filter->type == FILTER_TYPE_LOWPASS) {
        // Low-pass filter coefficients
        float one_minus_cos = 1.0f - cos_w;
        filter->b0 = one_minus_cos * 0.5f;
        filter->b1 = one_minus_cos;
        filter->b2 = one_minus_cos * 0.5f;
    } else {
        // High-pass filter coefficients
        float one_plus_cos = 1.0f + cos_w;
        filter->b0 = one_plus_cos * 0.5f;
        filter->b1 = -one_plus_cos;
        filter->b2 = one_plus_cos * 0.5f;
    }
    
    // Feedback coefficients (same for both LPF and HPF)
    a0 = 1.0f + alpha;
    filter->a1 = -2.0f * cos_w;
    filter->a2 = 1.0f - alpha;
    
    // Normalize by a0
    float inv_a0 = 1.0f / a0;
    filter->b0 *= inv_a0;
    filter->b1 *= inv_a0;
    filter->b2 *= inv_a0;
    filter->a1 *= inv_a0;
    filter->a2 *= inv_a0;
}

void resonant_filter_init(resonant_filter_t *filter, uint32_t sample_rate, filter_type_t type) {
    if (!filter) return;
    
    memset(filter, 0, sizeof(resonant_filter_t));
    
    filter->sample_rate = sample_rate;
    filter->type = type;
    filter->cutoff_hz = 1000.0f;    // Default 1kHz
    filter->resonance = 0.707f;      // Butterworth (no peak)
    filter->enabled = true;
    
    // Calculate initial coefficients
    resonant_filter_calc_coeffs(filter);
    
    ESP_LOGI(TAG, "Resonant %s filter initialized @ %lu Hz, cutoff=%.0f Hz, Q=%.2f",
             type == FILTER_TYPE_LOWPASS ? "LPF" : "HPF",
             (unsigned long)sample_rate,
             filter->cutoff_hz,
             filter->resonance);
}

void resonant_filter_set_cutoff(resonant_filter_t *filter, float cutoff_hz) {
    if (!filter) return;
    
    // Clamp to valid range
    if (cutoff_hz < FILTER_CUTOFF_MIN) cutoff_hz = FILTER_CUTOFF_MIN;
    if (cutoff_hz > FILTER_CUTOFF_MAX) cutoff_hz = FILTER_CUTOFF_MAX;
    
    filter->cutoff_hz = cutoff_hz;
    resonant_filter_calc_coeffs(filter);
}

void resonant_filter_set_resonance(resonant_filter_t *filter, float resonance) {
    if (!filter) return;
    
    // Clamp to valid range
    if (resonance < FILTER_Q_MIN) resonance = FILTER_Q_MIN;
    if (resonance > FILTER_Q_MAX) resonance = FILTER_Q_MAX;
    
    filter->resonance = resonance;
    resonant_filter_calc_coeffs(filter);
}

void resonant_filter_set_type(resonant_filter_t *filter, filter_type_t type) {
    if (!filter) return;
    
    filter->type = type;
    resonant_filter_calc_coeffs(filter);
}

void resonant_filter_set_enabled(resonant_filter_t *filter, bool enabled) {
    if (!filter) return;
    filter->enabled = enabled;
}

void resonant_filter_reset(resonant_filter_t *filter) {
    if (!filter) return;
    
    filter->w1_l = 0.0f;
    filter->w2_l = 0.0f;
    filter->w1_r = 0.0f;
    filter->w2_r = 0.0f;
}

void IRAM_ATTR resonant_filter_process(resonant_filter_t *filter, int16_t *buffer, size_t samples) {
    // Early exit if disabled or invalid
    if (!filter || !filter->enabled || !buffer || samples == 0) {
        return;
    }
    
    // Cache coefficients locally for better performance
    const float b0 = filter->b0;
    const float b1 = filter->b1;
    const float b2 = filter->b2;
    const float a1 = filter->a1;
    const float a2 = filter->a2;
    
    // Load state
    float w1_l = filter->w1_l;
    float w2_l = filter->w2_l;
    float w1_r = filter->w1_r;
    float w2_r = filter->w2_r;
    
    // Conversion scales
    const float scale_in = 1.0f / 32768.0f;
    const float scale_out = 32767.0f;
    
    // Process samples using transposed direct form II biquad
    // y[n] = b0*x[n] + w1[n-1]
    // w1[n] = b1*x[n] - a1*y[n] + w2[n-1]
    // w2[n] = b2*x[n] - a2*y[n]
    for (size_t i = 0; i < samples; i++) {
        // Left channel
        float x_l = (float)buffer[i * 2] * scale_in;
        float y_l = b0 * x_l + w1_l;
        w1_l = b1 * x_l - a1 * y_l + w2_l;
        w2_l = b2 * x_l - a2 * y_l;
        
        // Right channel
        float x_r = (float)buffer[i * 2 + 1] * scale_in;
        float y_r = b0 * x_r + w1_r;
        w1_r = b1 * x_r - a1 * y_r + w2_r;
        w2_r = b2 * x_r - a2 * y_r;
        
        // Convert back and clamp
        float out_l = y_l * scale_out;
        float out_r = y_r * scale_out;
        
        // Soft clipping for resonance peaks
        if (out_l > 32767.0f) out_l = 32767.0f;
        else if (out_l < -32768.0f) out_l = -32768.0f;
        if (out_r > 32767.0f) out_r = 32767.0f;
        else if (out_r < -32768.0f) out_r = -32768.0f;
        
        buffer[i * 2] = (int16_t)out_l;
        buffer[i * 2 + 1] = (int16_t)out_r;
    }
    
    // Store state (check for denormals)
    const float denormal_threshold = 1e-20f;
    filter->w1_l = (fabsf(w1_l) > denormal_threshold) ? w1_l : 0.0f;
    filter->w2_l = (fabsf(w2_l) > denormal_threshold) ? w2_l : 0.0f;
    filter->w1_r = (fabsf(w1_r) > denormal_threshold) ? w1_r : 0.0f;
    filter->w2_r = (fabsf(w2_r) > denormal_threshold) ? w2_r : 0.0f;
}

/* ============================================================================
 * DJ EQ Implementation
 * ============================================================================ */

void dj_eq_init(dj_eq_t *eq, uint32_t sample_rate) {
    memset(eq, 0, sizeof(dj_eq_t));
    eq->sample_rate = sample_rate;
    eq->gain_low = 0.0f;   // Unity gain (0.0 maps to 1.0x linear via gain_to_linear)
    eq->gain_mid = 0.0f;
    eq->gain_high = 0.0f;
    eq->enabled = true;
    
    // Initialize with flat response (identity/bypass filter)
    // Biquad identity: b0=1, b1=0, b2=0, a1=0, a2=0
    eq->coeffs_low[0] = 1.0f;
    eq->coeffs_mid[0] = 1.0f;
    eq->coeffs_high[0] = 1.0f;
    
    ESP_LOGI(TAG, "DJ EQ initialized @ %lu Hz", (unsigned long)sample_rate);
}

void dj_eq_set_gains(dj_eq_t *eq, float low, float mid, float high) {
    // Clamp to valid range [-1.0, 1.0]
    eq->gain_low = fmaxf(-1.0f, fminf(1.0f, low));
    eq->gain_mid = fmaxf(-1.0f, fminf(1.0f, mid));
    eq->gain_high = fmaxf(-1.0f, fminf(1.0f, high));
}

void dj_eq_set_enabled(dj_eq_t *eq, bool enabled) {
    eq->enabled = enabled;
}

void dj_eq_reset(dj_eq_t *eq) {
    // Clear all delay lines to prevent transients
    memset(eq->w_low_l, 0, sizeof(eq->w_low_l));
    memset(eq->w_low_r, 0, sizeof(eq->w_low_r));
    memset(eq->w_mid_l, 0, sizeof(eq->w_mid_l));
    memset(eq->w_mid_r, 0, sizeof(eq->w_mid_r));
    memset(eq->w_high_l, 0, sizeof(eq->w_high_l));
    memset(eq->w_high_r, 0, sizeof(eq->w_high_r));
}

/**
 * @brief Polynomial soft clipper: f(x) = x - x³/3
 * 
 * Provides tube-like saturation without harsh harmonics.
 * For inputs between -1.5 and 1.5, output is smoothly limited.
 * Outside this range, hard clips to ±1.0.
 * 
 * @param x Input sample (normalized -1.0 to 1.0, may exceed)
 * @return Soft-clipped output (guaranteed -1.0 to 1.0)
 */
static inline float IRAM_ATTR soft_clip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x - (x * x * x) / 3.0f;
}

/**
 * @brief Map gain from [-1, 1] to linear multiplier
 * 
 * -1.0 = mute (0.0x)
 *  0.0 = unity (1.0x)
 * +1.0 = boost (2.0x / +6dB)
 */
static inline float IRAM_ATTR gain_to_linear(float gain) {
    // Map [-1, 1] to [0, 2]
    return fmaxf(0.0f, gain + 1.0f);
}

void IRAM_ATTR dj_eq_process(dj_eq_t *eq, int16_t *buffer, size_t samples) {
    // Early exit if disabled or null
    if (!eq || !eq->enabled || !buffer || samples == 0) {
        return;
    }
    
    // Clamp samples to scratch buffer size
    if (samples > 256) {
        samples = 256;
    }
    
    // 1. Convert INT16 -> FLOAT (normalized to -1.0 to 1.0)
    const float scale_in = 1.0f / 32768.0f;
    for (size_t i = 0; i < samples * 2; i++) {
        scratch_buf[i] = (float)buffer[i] * scale_in;
    }
    
    // 2. Calculate linear gains from EQ settings
    float gain_l = gain_to_linear(eq->gain_low);
    float gain_m = gain_to_linear(eq->gain_mid);
    float gain_h = gain_to_linear(eq->gain_high);
    
    // Combined gain (simplified 3-band - real implementation would use biquads)
    // For now, average the three bands for a basic gain stage
    // TODO: Implement proper dsps_biquad_f32 filter chains
    float combined_gain = (gain_l + gain_m + gain_h) / 3.0f;
    
    // 3. Apply EQ gain and soft limiter
    for (size_t i = 0; i < samples; i++) {
        float l = scratch_buf[i * 2];
        float r = scratch_buf[i * 2 + 1];
        
        // Apply combined gain
        l *= combined_gain;
        r *= combined_gain;
        
        // Apply Soft Limiter (tube-like saturation)
        // f(x) = x - x³/3 for smooth clipping
        l = soft_clip(l);
        r = soft_clip(r);
        
        scratch_buf[i * 2] = l;
        scratch_buf[i * 2 + 1] = r;
    }
    
    // 4. Convert FLOAT -> INT16 with hard clipping safety
    const float scale_out = 32767.0f;
    for (size_t i = 0; i < samples * 2; i++) {
        float val = scratch_buf[i] * scale_out;
        // Final safety clamp (should rarely trigger after soft limiter)
        if (val > 32767.0f) val = 32767.0f;
        if (val < -32768.0f) val = -32768.0f;
        buffer[i] = (int16_t)val;
    }
}
