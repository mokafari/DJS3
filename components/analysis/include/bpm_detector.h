#pragma once
#include <stdint.h>
#include <stddef.h>

// BPM detection result
typedef struct {
    float bpm;              // Detected BPM (0 if detection failed)
    float confidence;       // Confidence score (0.0 - 1.0)
} bpm_result_t;

// Detect BPM from PCM audio
// samples: interleaved stereo 16-bit PCM at 44100 Hz
// num_samples: total sample count (both channels)
// Returns BPM result
bpm_result_t bpm_detect(const int16_t *samples, size_t num_samples);

// Progressive BPM detector for streaming
typedef struct bpm_detector_ctx bpm_detector_ctx_t;

// Create detector context
// sample_rate: typically 44100
// max_duration_sec: max audio to analyze (10-30 seconds is usually enough)
bpm_detector_ctx_t* bpm_detector_create(uint32_t sample_rate, uint32_t max_duration_sec);

// Feed samples to detector
void bpm_detector_feed(bpm_detector_ctx_t *ctx, const int16_t *samples, size_t count);

// Get current BPM estimate (can be called during feeding)
bpm_result_t bpm_detector_get_result(bpm_detector_ctx_t *ctx);

// Finalize and get result
bpm_result_t bpm_detector_finish(bpm_detector_ctx_t *ctx);

// Destroy context
void bpm_detector_destroy(bpm_detector_ctx_t *ctx);
