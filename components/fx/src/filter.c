/**
 * @file filter.c
 * @brief Resonant biquad filter implementation
 * 
 * Implements DJ-style resonant filter with:
 * - Direct Form II Transposed biquad for numerical stability
 * - RBJ Audio EQ Cookbook coefficients
 * - IRAM optimization for ESP32-S3
 */

#include "filter.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "filter";

// Constants
#define FILTER_CUTOFF_MIN    20.0f
#define FILTER_CUTOFF_MAX    20000.0f
#define FILTER_Q_MIN         0.5f
#define FILTER_Q_MAX         20.0f
#define FILTER_DEFAULT_CUTOFF 1000.0f
#define FILTER_DEFAULT_Q     0.707107f  // Butterworth (1/sqrt(2))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Clamp float value to range
 */
static inline float clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/**
 * @brief Calculate biquad coefficients using RBJ Audio EQ Cookbook formulas
 * 
 * Reference: https://www.w3.org/2011/audio/audio-eq-cookbook.html
 */
static void filter_calculate_coefficients(resonant_filter_t *filter) {
    if (!filter->coeffs_dirty) {
        return;
    }
    
    float fc = filter->cutoff_hz;
    float Q = filter->resonance;
    float fs = (float)filter->sample_rate;
    
    // Clamp frequency to valid range (leave headroom for stability)
    fc = clampf(fc, FILTER_CUTOFF_MIN, fs * 0.45f);
    
    // Normalized frequency (0 to π)
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * Q);
    
    float b0, b1, b2, a0, a1, a2;
    
    if (filter->mode == FILTER_MODE_LOWPASS) {
        // Low-pass filter coefficients (RBJ)
        b0 = (1.0f - cos_w0) / 2.0f;
        b1 = 1.0f - cos_w0;
        b2 = (1.0f - cos_w0) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha;
    } else {
        // High-pass filter coefficients (RBJ)
        b0 = (1.0f + cos_w0) / 2.0f;
        b1 = -(1.0f + cos_w0);
        b2 = (1.0f + cos_w0) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha;
    }
    
    // Normalize by a0 (so a0 becomes 1.0)
    float inv_a0 = 1.0f / a0;
    filter->b0 = b0 * inv_a0;
    filter->b1 = b1 * inv_a0;
    filter->b2 = b2 * inv_a0;
    filter->a1 = a1 * inv_a0;
    filter->a2 = a2 * inv_a0;
    
    filter->coeffs_dirty = false;
    
    ESP_LOGD(TAG, "Filter coeffs: fc=%.1f Q=%.2f mode=%s",
             fc, Q, filter->mode == FILTER_MODE_LOWPASS ? "LP" : "HP");
    ESP_LOGD(TAG, "  b=[%.6f, %.6f, %.6f] a=[1, %.6f, %.6f]",
             filter->b0, filter->b1, filter->b2, filter->a1, filter->a2);
}

void filter_init(resonant_filter_t *filter, uint32_t sample_rate) {
    if (!filter) return;
    
    memset(filter, 0, sizeof(resonant_filter_t));
    
    filter->sample_rate = sample_rate;
    filter->cutoff_hz = FILTER_DEFAULT_CUTOFF;
    filter->resonance = FILTER_DEFAULT_Q;
    filter->mode = FILTER_MODE_LOWPASS;
    filter->enabled = false;  // Start bypassed
    filter->coeffs_dirty = true;
    
    // Calculate initial coefficients
    filter_calculate_coefficients(filter);
    
    ESP_LOGI(TAG, "Resonant filter initialized @ %lu Hz", (unsigned long)sample_rate);
}

void filter_set_cutoff(resonant_filter_t *filter, float cutoff_hz) {
    if (!filter) return;
    
    cutoff_hz = clampf(cutoff_hz, FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX);
    
    if (filter->cutoff_hz != cutoff_hz) {
        filter->cutoff_hz = cutoff_hz;
        filter->coeffs_dirty = true;
    }
}

void filter_set_resonance(resonant_filter_t *filter, float resonance) {
    if (!filter) return;
    
    resonance = clampf(resonance, FILTER_Q_MIN, FILTER_Q_MAX);
    
    if (filter->resonance != resonance) {
        filter->resonance = resonance;
        filter->coeffs_dirty = true;
    }
}

void filter_set_mode(resonant_filter_t *filter, filter_mode_t mode) {
    if (!filter) return;
    
    if (filter->mode != mode) {
        filter->mode = mode;
        filter->coeffs_dirty = true;
        // Reset state to prevent transients when switching modes
        filter_reset(filter);
    }
}

void filter_set_enabled(resonant_filter_t *filter, bool enabled) {
    if (!filter) return;
    
    if (!filter->enabled && enabled) {
        // Enabling - clear state to prevent transients
        filter_reset(filter);
    }
    filter->enabled = enabled;
}

/**
 * @brief Process single sample through biquad filter (Direct Form II Transposed)
 * 
 * y[n] = b0*x[n] + z1
 * z1   = b1*x[n] - a1*y[n] + z2
 * z2   = b2*x[n] - a2*y[n]
 */
static inline float IRAM_ATTR biquad_process(
    float x, 
    biquad_state_t *state,
    float b0, float b1, float b2,
    float a1, float a2
) {
    // Output
    float y = b0 * x + state->z1;
    
    // Update state
    state->z1 = b1 * x - a1 * y + state->z2;
    state->z2 = b2 * x - a2 * y;
    
    return y;
}

void IRAM_ATTR filter_process(resonant_filter_t *filter, int16_t *buffer, size_t num_frames) {
    if (!filter || !buffer || num_frames == 0) {
        return;
    }
    
    // Bypass if disabled
    if (!filter->enabled) {
        return;
    }
    
    // Recalculate coefficients if parameters changed
    if (filter->coeffs_dirty) {
        filter_calculate_coefficients(filter);
    }
    
    // Cache coefficients for tight loop
    const float b0 = filter->b0;
    const float b1 = filter->b1;
    const float b2 = filter->b2;
    const float a1 = filter->a1;
    const float a2 = filter->a2;
    
    // Scaling factors for int16 <-> float conversion
    const float scale_in = 1.0f / 32768.0f;
    const float scale_out = 32767.0f;
    
    // Local state pointers
    biquad_state_t *state_l = &filter->state_l;
    biquad_state_t *state_r = &filter->state_r;
    
    // Process stereo frames
    for (size_t i = 0; i < num_frames; i++) {
        size_t idx = i * 2;
        
        // Convert to float (-1.0 to 1.0)
        float in_l = (float)buffer[idx] * scale_in;
        float in_r = (float)buffer[idx + 1] * scale_in;
        
        // Apply biquad filter
        float out_l = biquad_process(in_l, state_l, b0, b1, b2, a1, a2);
        float out_r = biquad_process(in_r, state_r, b0, b1, b2, a1, a2);
        
        // Convert back to int16 with clamping
        float val_l = out_l * scale_out;
        float val_r = out_r * scale_out;
        
        // Clamp to int16 range
        if (val_l > 32767.0f) val_l = 32767.0f;
        else if (val_l < -32768.0f) val_l = -32768.0f;
        
        if (val_r > 32767.0f) val_r = 32767.0f;
        else if (val_r < -32768.0f) val_r = -32768.0f;
        
        buffer[idx] = (int16_t)val_l;
        buffer[idx + 1] = (int16_t)val_r;
    }
}

void filter_reset(resonant_filter_t *filter) {
    if (!filter) return;
    
    // Clear delay line state
    memset(&filter->state_l, 0, sizeof(biquad_state_t));
    memset(&filter->state_r, 0, sizeof(biquad_state_t));
    
    ESP_LOGD(TAG, "Filter state reset");
}

float filter_get_cutoff(const resonant_filter_t *filter) {
    return filter ? filter->cutoff_hz : 0.0f;
}

float filter_get_resonance(const resonant_filter_t *filter) {
    return filter ? filter->resonance : 0.0f;
}

filter_mode_t filter_get_mode(const resonant_filter_t *filter) {
    return filter ? filter->mode : FILTER_MODE_LOWPASS;
}
