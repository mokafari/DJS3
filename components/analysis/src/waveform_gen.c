/**
 * @file waveform_gen.c
 * @brief Waveform overview generator implementation
 */

#include "waveform_gen.h"
#include <stdlib.h>
#include <string.h>

/** Context for progressive waveform generation */
struct waveform_gen_ctx {
    size_t total_samples;           /**< Expected total stereo frames */
    size_t samples_per_bin;         /**< Samples per output bin */
    size_t samples_processed;       /**< Running count of processed frames */
    uint16_t peaks[WAVEFORM_OUTPUT_POINTS];  /**< Peak values per bin (0-32767) */
};

/**
 * @brief Get absolute value of int16_t safely (handles INT16_MIN)
 */
static inline uint16_t abs16(int16_t val) {
    if (val == INT16_MIN) return 32767;  // Clamp to avoid overflow
    return (uint16_t)(val < 0 ? -val : val);
}

/**
 * @brief Find peak amplitude from a stereo sample pair
 */
static inline uint16_t stereo_peak(int16_t left, int16_t right) {
    uint16_t l = abs16(left);
    uint16_t r = abs16(right);
    return (l > r) ? l : r;
}

int waveform_gen_from_pcm(const int16_t *samples, size_t num_samples, 
                          uint8_t output[WAVEFORM_OUTPUT_POINTS]) {
    if (!samples || !output || num_samples == 0) {
        return -1;
    }
    
    // Calculate samples per bin (handle small files)
    size_t samples_per_bin = num_samples / WAVEFORM_OUTPUT_POINTS;
    if (samples_per_bin == 0) samples_per_bin = 1;
    
    // Process each bin
    for (int bin = 0; bin < WAVEFORM_OUTPUT_POINTS; bin++) {
        size_t start = bin * samples_per_bin;
        size_t end = start + samples_per_bin;
        
        // Last bin gets remaining samples
        if (bin == WAVEFORM_OUTPUT_POINTS - 1) {
            end = num_samples;
        }
        
        // Clamp to valid range
        if (start >= num_samples) {
            output[bin] = 0;
            continue;
        }
        if (end > num_samples) {
            end = num_samples;
        }
        
        // Find peak in this bin (samples are interleaved L,R,L,R...)
        uint16_t peak = 0;
        for (size_t i = start; i < end; i++) {
            // Index into interleaved stereo: left = i*2, right = i*2+1
            int16_t left = samples[i * 2];
            int16_t right = samples[i * 2 + 1];
            uint16_t val = stereo_peak(left, right);
            if (val > peak) peak = val;
        }
        
        // Normalize to 0-255 (32767 -> 255)
        output[bin] = (uint8_t)((peak * 255) / 32767);
    }
    
    return 0;
}

waveform_gen_ctx_t* waveform_gen_create(size_t total_samples) {
    if (total_samples == 0) {
        return NULL;
    }
    
    waveform_gen_ctx_t *ctx = calloc(1, sizeof(waveform_gen_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->total_samples = total_samples;
    ctx->samples_per_bin = total_samples / WAVEFORM_OUTPUT_POINTS;
    if (ctx->samples_per_bin == 0) ctx->samples_per_bin = 1;
    ctx->samples_processed = 0;
    
    // peaks[] already zeroed by calloc
    
    return ctx;
}

void waveform_gen_feed(waveform_gen_ctx_t *ctx, const int16_t *samples, size_t count) {
    if (!ctx || !samples || count == 0) {
        return;
    }
    
    // Process each sample in the chunk
    for (size_t i = 0; i < count; i++) {
        size_t sample_index = ctx->samples_processed + i;
        
        // Determine which bin this sample belongs to
        size_t bin = sample_index / ctx->samples_per_bin;
        if (bin >= WAVEFORM_OUTPUT_POINTS) {
            bin = WAVEFORM_OUTPUT_POINTS - 1;  // Last bin catches overflow
        }
        
        // Get stereo sample (interleaved L,R,L,R...)
        int16_t left = samples[i * 2];
        int16_t right = samples[i * 2 + 1];
        uint16_t val = stereo_peak(left, right);
        
        // Update peak for this bin
        if (val > ctx->peaks[bin]) {
            ctx->peaks[bin] = val;
        }
    }
    
    ctx->samples_processed += count;
}

void waveform_gen_finish(waveform_gen_ctx_t *ctx, uint8_t output[WAVEFORM_OUTPUT_POINTS]) {
    if (!ctx || !output) {
        return;
    }
    
    // Normalize all peaks to 0-255
    for (int bin = 0; bin < WAVEFORM_OUTPUT_POINTS; bin++) {
        output[bin] = (uint8_t)((ctx->peaks[bin] * 255) / 32767);
    }
}

void waveform_gen_destroy(waveform_gen_ctx_t *ctx) {
    if (ctx) {
        free(ctx);
    }
}
