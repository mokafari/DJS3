/**
 * @file dsp_engine.h
 * @brief DSP engine for real-time audio processing
 * 
 * Provides fixed-point linear interpolation resampler for variable-speed
 * playback on ESP32-S3 with cache-optimized IRAM functions.
 */

#ifndef DSP_ENGINE_H
#define DSP_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fixed point Q16.16 constants
#define FP_SHIFT 16
#define FP_ONE   (1 << FP_SHIFT)
#define FP_MASK  0xFFFF

// Block size for DSP processing (stereo frames)
// 256 frames = 1024 bytes = ~5.8ms at 44.1kHz
#define DSP_BLOCK_SIZE 256

/**
 * @brief Resampler state (maintains phase between blocks)
 */
typedef struct {
    uint32_t phase_accum;  // Q16.16 fractional position carried between blocks
} resampler_state_t;

/**
 * @brief Initialize resampler state
 * @param state Pointer to resampler state structure
 */
void dsp_resampler_init(resampler_state_t *state);

/**
 * @brief Reset resampler state (e.g., on seek or track change)
 * @param state Pointer to resampler state structure
 */
void dsp_resampler_reset(resampler_state_t *state);

/**
 * @brief Resample audio with linear interpolation (IRAM optimized)
 * 
 * Reads from a ring buffer at variable speed and outputs fixed-size blocks.
 * Uses fixed-point Q16.16 math for cache-efficient operation.
 * 
 * @param state         Resampler state (phase accumulator, updated)
 * @param input_ring    Ring buffer (stereo interleaved int16)
 * @param ring_size_samples Size of ring buffer in STEREO FRAMES (bytes / 4)
 * @param read_head_idx Pointer to current read index in samples (updated)
 * @param output        Output buffer (stereo interleaved int16)
 * @param output_samples Number of stereo frames to generate (e.g., 256)
 * @param speed_ratio   Playback speed (1.0 = normal, 0.5 = half, 2.0 = double)
 * @return Number of source samples consumed (for buffer tracking)
 * 
 * @note Thread Safety: Caller must hold buffer_mutex when accessing ring buffer
 * 
 * Example:
 *     size_t consumed = dsp_resample_linear(
 *         &state,
 *         (const int16_t*)pcm_ring_buffer,
 *         RING_BUFFER_SIZE / 4,
 *         &rb_read_head_index,
 *         i2s_block,
 *         DSP_BLOCK_SIZE,
 *         1.05f  // +5% pitch
 *     );
 */
size_t dsp_resample_linear(
    resampler_state_t *state,
    const int16_t *input_ring,
    size_t ring_size_samples,
    size_t *read_head_idx,
    int16_t *output,
    size_t output_samples,
    float speed_ratio
);

#ifdef __cplusplus
}
#endif

#endif // DSP_ENGINE_H
