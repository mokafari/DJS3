/**
 * @file filter.c
 * @brief Resonant biquad filter implementation
 * 
 * Implements Direct Form II Transposed biquad filter with:
 * - Low-pass and high-pass modes
 * - Resonance (Q factor) control
 * - Optimized for ESP32-S3 with IRAM placement
 * 
 * Cookbook reference: Audio EQ Cookbook by Robert Bristow-Johnson
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

// Parameter limits
#define CUTOFF_MIN_HZ   20.0f
#define CUTOFF_MAX_HZ   20000.0f
#define RESONANCE_MIN   0.5f
#define RESONANCE_MAX   20.0f
#define RESONANCE_DEFAULT 0.707107f  // Butterworth (1/sqrt(2))

// Clamp helper
static inline float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * @brief Calculate biquad coefficients for current parameters
 * 
 * Uses Audio EQ Cookbook formulas for LPF/HPF.
 * Coefficients are normalized (a0 = 1.0).
 */
static void filter_calc_coefficients(resonant_filter_t *filter) {
    if (!filter || !filter->coeffs_dirty) return;
    
    // Clamp cutoff to Nyquist limit (with margin for stability)
    float nyquist = (float)filter->sample_rate * 0.5f;
    float fc = clampf(filter->cutoff_hz, CUTOFF_MIN_HZ, fminf(CUTOFF_MAX_HZ, nyquist * 0.95f));
    float q = clampf(filter->resonance, RESONANCE_MIN, RESONANCE_MAX);
    
    // Pre-warp frequency for bilinear transform
    float omega = 2.0f * M_PI * fc / (float)filter->sample_rate;
    float sin_omega = sinf(omega);
    float cos_omega = cosf(omega);
    float alpha = sin_omega / (2.0f * q);
    
    float b0, b1, b2, a0, a1, a2;
    
    if (filter->mode == FILTER_MODE_LOWPASS) {
        // Low-pass filter coefficients
        b0 = (1.0f - cos_omega) * 0.5f;
        b1 = 1.0f - cos_omega;
        b2 = (1.0f - cos_omega) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_omega;
        a2 = 1.0f - alpha;
    } else {
        // High-pass filter coefficients
        b0 = (1.0f + cos_omega) * 0.5f;
        b1 = -(1.0f + cos_omega);
        b2 = (1.0f + cos_omega) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_omega;
        a2 = 1.0f - alpha;
    }
    
    // Normalize coefficients (divide by a0)
    float a0_inv = 1.0f / a0;
    filter->b0 = b0 * a0_inv;
    filter->b1 = b1 * a0_inv;
    filter->b2 = b2 * a0_inv;
    filter->a1 = a1 * a0_inv;
    filter->a2 = a2 * a0_inv;
    
    filter->coeffs_dirty = false;
    
    ESP_LOGD(TAG, "Coeffs: fc=%.1f Q=%.2f mode=%s b=[%.4f,%.4f,%.4f] a=[1,%.4f,%.4f]",
             fc, q, filter->mode == FILTER_MODE_LOWPASS ? "LP" : "HP",
             filter->b0, filter->b1, filter->b2, filter->a1, filter->a2);
}

void filter_init(resonant_filter_t *filter, uint32_t sample_rate) {
    if (!filter) return;
    
    memset(filter, 0, sizeof(resonant_filter_t));
    
    filter->sample_rate = sample_rate;
    filter->cutoff_hz = 1000.0f;
    filter->resonance = RESONANCE_DEFAULT;
    filter->mode = FILTER_MODE_LOWPASS;
    filter->enabled = false;  // Start bypassed
    filter->coeffs_dirty = true;
    
    // Calculate initial coefficients
    filter_calc_coefficients(filter);
    
    ESP_LOGI(TAG, "Resonant filter initialized @ %lu Hz", (unsigned long)sample_rate);
}

void filter_set_cutoff(resonant_filter_t *filter, float cutoff_hz) {
    if (!filter) return;
    
    float clamped = clampf(cutoff_hz, CUTOFF_MIN_HZ, CUTOFF_MAX_HZ);
    if (filter->cutoff_hz != clamped) {
        filter->cutoff_hz = clamped;
        filter->coeffs_dirty = true;
    }
}

void filter_set_resonance(resonant_filter_t *filter, float resonance) {
    if (!filter) return;
    
    float clamped = clampf(resonance, RESONANCE_MIN, RESONANCE_MAX);
    if (filter->resonance != clamped) {
        filter->resonance = clamped;
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
        // Enabling - reset state to prevent transients
        filter_reset(filter);
    }
    filter->enabled = enabled;
}

void filter_reset(resonant_filter_t *filter) {
    if (!filter) return;
    
    // Clear delay line state
    filter->state_l.z1 = 0.0f;
    filter->state_l.z2 = 0.0f;
    filter->state_r.z1 = 0.0f;
    filter->state_r.z2 = 0.0f;
}

float filter_get_cutoff(const resonant_filter_t *filter) {
    return filter ? filter->cutoff_hz : 0.0f;
}

float filter_get_resonance(const resonant_filter_t *filter) {
    return filter ? filter->resonance : 0.0f;
}

filter_mode_t filter_get_mode(const resonant_filter_t *filter) {
    if (!filter) return FILTER_MODE_LOWPASS;
    return filter->mode;
}

void IRAM_ATTR filter_process(resonant_filter_t *filter, int16_t *buffer, size_t num_frames) {
    // Early exit checks
    if (!filter || !buffer || num_frames == 0) {
        return;
    }
    
    // Bypass if disabled
    if (!filter->enabled) {
        return;
    }
    
    // Recalculate coefficients if parameters changed
    if (filter->coeffs_dirty) {
        filter_calc_coefficients(filter);
    }
    
    // Cache coefficients in local variables for speed
    const float b0 = filter->b0;
    const float b1 = filter->b1;
    const float b2 = filter->b2;
    const float a1 = filter->a1;
    const float a2 = filter->a2;
    
    // Cache state in local variables
    float z1_l = filter->state_l.z1;
    float z2_l = filter->state_l.z2;
    float z1_r = filter->state_r.z1;
    float z2_r = filter->state_r.z2;
    
    // Scaling factors
    const float scale_in = 1.0f / 32768.0f;
    const float scale_out = 32767.0f;
    
    // Process all frames
    for (size_t i = 0; i < num_frames; i++) {
        // Convert to float (normalized -1.0 to ~1.0)
        float in_l = (float)buffer[i * 2] * scale_in;
        float in_r = (float)buffer[i * 2 + 1] * scale_in;
        
        // Apply biquad filter (Direct Form II Transposed)
        // y[n] = b0*x[n] + z1
        // z1   = b1*x[n] - a1*y[n] + z2
        // z2   = b2*x[n] - a2*y[n]
        
        // Left channel
        float out_l = b0 * in_l + z1_l;
        z1_l = b1 * in_l - a1 * out_l + z2_l;
        z2_l = b2 * in_l - a2 * out_l;
        
        // Right channel
        float out_r = b0 * in_r + z1_r;
        z1_r = b1 * in_r - a1 * out_r + z2_r;
        z2_r = b2 * in_r - a2 * out_r;
        
        // Convert back to int16 with clipping
        float val_l = out_l * scale_out;
        float val_r = out_r * scale_out;
        
        // Clamp to int16 range (resonance can cause overshoot)
        if (val_l > 32767.0f) val_l = 32767.0f;
        else if (val_l < -32768.0f) val_l = -32768.0f;
        if (val_r > 32767.0f) val_r = 32767.0f;
        else if (val_r < -32768.0f) val_r = -32768.0f;
        
        buffer[i * 2] = (int16_t)val_l;
        buffer[i * 2 + 1] = (int16_t)val_r;
    }
    
    // Store state back (with denormal protection)
    // Flush very small values to zero to prevent denormal slowdown
    const float denormal_threshold = 1e-20f;
    if (fabsf(z1_l) < denormal_threshold) z1_l = 0.0f;
    if (fabsf(z2_l) < denormal_threshold) z2_l = 0.0f;
    if (fabsf(z1_r) < denormal_threshold) z1_r = 0.0f;
    if (fabsf(z2_r) < denormal_threshold) z2_r = 0.0f;
    
    filter->state_l.z1 = z1_l;
    filter->state_l.z2 = z2_l;
    filter->state_r.z1 = z1_r;
    filter->state_r.z2 = z2_r;
}
