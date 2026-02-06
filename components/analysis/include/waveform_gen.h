/**
 * @file waveform_gen.h
 * @brief Waveform overview generator for track visualization
 * 
 * Generates 480-point amplitude overview for the track navigation stripe.
 * Supports both one-shot (full track in memory) and streaming modes.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of points in the waveform overview (matches display width) */
#define WAVEFORM_OUTPUT_POINTS 480

/**
 * @brief Generate waveform overview from raw PCM samples (one-shot)
 * 
 * Processes entire track at once. Use for small files or when full
 * PCM buffer is available.
 * 
 * @param samples     Pointer to interleaved stereo 16-bit PCM samples
 * @param num_samples Total number of sample frames (stereo pairs)
 * @param output      480-byte array to receive normalized peaks (0-255)
 * @return 0 on success, -1 on invalid input
 */
int waveform_gen_from_pcm(const int16_t *samples, size_t num_samples, 
                          uint8_t output[WAVEFORM_OUTPUT_POINTS]);

/**
 * @brief Progressive waveform generator context
 * 
 * Opaque structure for streaming waveform generation.
 * Use when processing track in chunks (typical for ESP32 with limited RAM).
 */
typedef struct waveform_gen_ctx waveform_gen_ctx_t;

/**
 * @brief Create a progressive waveform generator
 * 
 * @param total_samples Expected total number of stereo sample frames
 * @return Pointer to context, or NULL on allocation failure
 */
waveform_gen_ctx_t* waveform_gen_create(size_t total_samples);

/**
 * @brief Feed PCM samples to the progressive generator
 * 
 * Call repeatedly with chunks of audio data. The generator tracks
 * which bin each chunk belongs to and accumulates peak values.
 * 
 * @param ctx     Generator context from waveform_gen_create()
 * @param samples Pointer to interleaved stereo 16-bit PCM chunk
 * @param count   Number of stereo sample frames in this chunk
 */
void waveform_gen_feed(waveform_gen_ctx_t *ctx, const int16_t *samples, size_t count);

/**
 * @brief Finalize and extract the waveform overview
 * 
 * Call after all samples have been fed. Normalizes peaks to 0-255 range.
 * 
 * @param ctx    Generator context
 * @param output 480-byte array to receive the waveform
 */
void waveform_gen_finish(waveform_gen_ctx_t *ctx, uint8_t output[WAVEFORM_OUTPUT_POINTS]);

/**
 * @brief Destroy the generator and free resources
 * 
 * @param ctx Generator context (NULL-safe)
 */
void waveform_gen_destroy(waveform_gen_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
