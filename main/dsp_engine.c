/**
 * @file dsp_engine.c
 * @brief DSP engine implementation with fixed-point resampler
 * 
 * Optimized for ESP32-S3 with IRAM placement and cache-friendly access patterns.
 */

#include "dsp_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "dsp_engine";

void dsp_resampler_init(resampler_state_t *state) {
    memset(state, 0, sizeof(resampler_state_t));
    ESP_LOGI(TAG, "Resampler initialized (Q16.16 fixed-point)");
}

void dsp_resampler_reset(resampler_state_t *state) {
    state->phase_accum = 0;
}

size_t IRAM_ATTR dsp_resample_linear(
    resampler_state_t *state,
    const int16_t *input_ring,
    size_t ring_size_samples,
    size_t *read_head_idx,
    int16_t *output,
    size_t output_samples,
    float speed_ratio
) {
    // Convert float speed to fixed-point step
    // speed_ratio of 1.0 = step of 65536 (FP_ONE)
    uint32_t step = (uint32_t)(speed_ratio * (float)FP_ONE);
    uint32_t position_fp = state->phase_accum;
    size_t current_read_idx = *read_head_idx;
    
    for (size_t i = 0; i < output_samples; i++) {
        // 1. Calculate integer offset and fractional part
        uint32_t offset = position_fp >> FP_SHIFT;
        uint16_t frac = position_fp & FP_MASK;
        
        // 2. Calculate safe ring buffer indices (WRAP-AROUND SAFE)
        // idx1: Current sample position
        // idx2: Next sample for interpolation (handles wrap-around)
        size_t idx1 = (current_read_idx + offset) % ring_size_samples;
        size_t idx2 = (idx1 + 1) % ring_size_samples;
        
        // 3. Fetch Stereo Samples
        // Buffer layout: L0, R0, L1, R1, ... (interleaved)
        // So sample index * 2 gives left channel, + 1 gives right
        int16_t l1 = input_ring[idx1 * 2];
        int16_t r1 = input_ring[idx1 * 2 + 1];
        int16_t l2 = input_ring[idx2 * 2];
        int16_t r2 = input_ring[idx2 * 2 + 1];
        
        // 4. Linear Interpolation (Integer Math)
        // output = sample1 + (fraction * (sample2 - sample1)) >> 16
        // This is cache-efficient fixed-point interpolation
        int32_t delta_l = l2 - l1;
        int32_t delta_r = r2 - r1;
        
        output[i * 2]     = l1 + (int16_t)((delta_l * frac) >> FP_SHIFT);
        output[i * 2 + 1] = r1 + (int16_t)((delta_r * frac) >> FP_SHIFT);
        
        // 5. Advance Phase Accumulator
        position_fp += step;
    }
    
    // 6. Update State for next block
    // Calculate total integer samples consumed
    uint32_t samples_advanced = position_fp >> FP_SHIFT;
    
    // Update Ring Buffer Read Head (with wrap-around)
    *read_head_idx = (current_read_idx + samples_advanced) % ring_size_samples;
    
    // Keep only fractional part for seamless continuation next block
    state->phase_accum = position_fp & FP_MASK;
    
    return samples_advanced;
}
