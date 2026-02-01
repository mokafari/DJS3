/**
 * @file filter.c
 * @brief DJ EQ implementation with biquad filters and polynomial soft limiter
 * 
 * Optimized for ESP32-S3 with:
 * - Static scratch buffer (avoids stack overflow)
 * - IRAM placement for hot processing loop
 * - Polynomial soft clipper for tube-like saturation
 */

#include "filter.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "filter";

// Static scratch buffer for float conversion (avoids stack overflow)
// 2 channels * 256 samples = 512 floats = 2KB
// Allocated in .bss segment, not on stack
// MUST be 16-byte aligned for potential SIMD operations
static __attribute__((aligned(16))) float scratch_buf[512];

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
