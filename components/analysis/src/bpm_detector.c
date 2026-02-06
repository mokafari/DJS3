/**
 * BPM Detector for ESP32
 * 
 * Algorithm: Energy-based onset detection + Inter-Onset Interval histogram
 * 
 * Designed for ESP32 constraints:
 * - Minimal memory footprint
 * - No FFT required (simpler than spectral flux)
 * - Works with streaming audio
 * 
 * Steps:
 * 1. Convert stereo to mono
 * 2. Calculate energy in sliding windows
 * 3. Detect onsets (significant energy increases)
 * 4. Build histogram of inter-onset intervals
 * 5. Find dominant IOI → convert to BPM
 */

#include "bpm_detector.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Algorithm parameters
#define WINDOW_SIZE     1024    // Samples per energy window
#define HOP_SIZE        512     // Samples between windows (50% overlap)
#define MAX_ONSETS      500     // Maximum onsets to track
#define BPM_MIN         60.0f   // Minimum BPM to detect
#define BPM_MAX         180.0f  // Maximum BPM to detect
#define ONSET_THRESHOLD 1.5f    // Energy ratio for onset detection (50% increase)
#define MIN_ONSET_GAP   0.05f   // Minimum seconds between onsets (avoids double-triggers)
#define IOI_BIN_SIZE    0.01f   // Histogram bin size in seconds
#define IOI_MIN         0.25f   // Min IOI (240 BPM)
#define IOI_MAX         1.0f    // Max IOI (60 BPM)
#define IOI_BINS        ((int)((IOI_MAX - IOI_MIN) / IOI_BIN_SIZE))
#define MIN_HISTOGRAM_COUNT 5   // Minimum hits in a bin to consider valid

struct bpm_detector_ctx {
    uint32_t sample_rate;
    
    // Sample buffer (mono, downsampled from stereo input)
    int16_t *sample_buffer;
    size_t sample_count;
    size_t sample_capacity;
    
    // Energy analysis
    float *energy_buffer;
    size_t energy_count;
    size_t energy_capacity;
    
    // Onset times
    float *onset_times;
    size_t onset_count;
    
    // Cached result
    bpm_result_t cached_result;
    int result_valid;
};

// Forward declarations
static float calculate_window_energy(const int16_t *samples, size_t count);
static void detect_onsets(bpm_detector_ctx_t *ctx);
static float find_dominant_ioi(bpm_detector_ctx_t *ctx, float *confidence);
static float calculate_confidence(bpm_detector_ctx_t *ctx, float ioi);

bpm_detector_ctx_t* bpm_detector_create(uint32_t sample_rate, uint32_t max_duration_sec) {
    bpm_detector_ctx_t *ctx = calloc(1, sizeof(bpm_detector_ctx_t));
    if (!ctx) return NULL;
    
    ctx->sample_rate = sample_rate;
    ctx->result_valid = 0;
    
    // Allocate sample buffer (mono samples)
    ctx->sample_capacity = sample_rate * max_duration_sec;
    ctx->sample_buffer = malloc(ctx->sample_capacity * sizeof(int16_t));
    if (!ctx->sample_buffer) {
        free(ctx);
        return NULL;
    }
    
    // Allocate energy buffer
    ctx->energy_capacity = ctx->sample_capacity / HOP_SIZE + 1;
    ctx->energy_buffer = malloc(ctx->energy_capacity * sizeof(float));
    if (!ctx->energy_buffer) {
        free(ctx->sample_buffer);
        free(ctx);
        return NULL;
    }
    
    // Allocate onset times buffer
    ctx->onset_times = malloc(MAX_ONSETS * sizeof(float));
    if (!ctx->onset_times) {
        free(ctx->energy_buffer);
        free(ctx->sample_buffer);
        free(ctx);
        return NULL;
    }
    
    ctx->sample_count = 0;
    ctx->energy_count = 0;
    ctx->onset_count = 0;
    
    return ctx;
}

void bpm_detector_feed(bpm_detector_ctx_t *ctx, const int16_t *samples, size_t count) {
    if (!ctx || !samples || count == 0) return;
    
    // Invalidate cached result when new samples arrive
    ctx->result_valid = 0;
    
    // Convert interleaved stereo to mono and store
    // Input: L0, R0, L1, R1, L2, R2, ...
    // Output: mono samples
    for (size_t i = 0; i + 1 < count && ctx->sample_count < ctx->sample_capacity; i += 2) {
        int32_t left = samples[i];
        int32_t right = samples[i + 1];
        int32_t mono = (left + right) / 2;
        ctx->sample_buffer[ctx->sample_count++] = (int16_t)mono;
    }
}

static float calculate_window_energy(const int16_t *samples, size_t count) {
    float energy = 0.0f;
    for (size_t i = 0; i < count; i++) {
        // Normalize to [-1, 1] range
        float s = samples[i] / 32768.0f;
        energy += s * s;
    }
    return energy / count;  // Return mean energy
}

static void detect_onsets(bpm_detector_ctx_t *ctx) {
    if (!ctx || ctx->sample_count < WINDOW_SIZE) return;
    
    // Calculate energy for each window
    ctx->energy_count = 0;
    for (size_t i = 0; i + WINDOW_SIZE <= ctx->sample_count; i += HOP_SIZE) {
        if (ctx->energy_count >= ctx->energy_capacity) break;
        ctx->energy_buffer[ctx->energy_count++] = 
            calculate_window_energy(&ctx->sample_buffer[i], WINDOW_SIZE);
    }
    
    // Detect onsets: points where energy increases significantly
    ctx->onset_count = 0;
    float last_onset_time = -MIN_ONSET_GAP;  // Allow first onset at t=0
    
    // Calculate mean energy for adaptive threshold
    float mean_energy = 0.0f;
    for (size_t i = 0; i < ctx->energy_count; i++) {
        mean_energy += ctx->energy_buffer[i];
    }
    mean_energy /= ctx->energy_count;
    float energy_floor = mean_energy * 0.1f;  // Minimum energy to consider
    
    for (size_t i = 1; i < ctx->energy_count && ctx->onset_count < MAX_ONSETS; i++) {
        float prev_energy = ctx->energy_buffer[i - 1];
        float curr_energy = ctx->energy_buffer[i];
        
        // Skip very low energy regions (silence)
        if (curr_energy < energy_floor) continue;
        
        // Check for significant energy increase
        if (prev_energy > 0 && curr_energy > prev_energy * ONSET_THRESHOLD) {
            float time = (float)(i * HOP_SIZE) / ctx->sample_rate;
            
            // Enforce minimum gap between onsets
            if (time - last_onset_time >= MIN_ONSET_GAP) {
                ctx->onset_times[ctx->onset_count++] = time;
                last_onset_time = time;
            }
        }
    }
}

static float find_dominant_ioi(bpm_detector_ctx_t *ctx, float *confidence) {
    if (!ctx || ctx->onset_count < 2) {
        *confidence = 0.0f;
        return 0.0f;
    }
    
    // Build histogram of inter-onset intervals
    int histogram[IOI_BINS];
    memset(histogram, 0, sizeof(histogram));
    
    int total_iois = 0;
    
    for (size_t i = 1; i < ctx->onset_count; i++) {
        float ioi = ctx->onset_times[i] - ctx->onset_times[i - 1];
        
        // Only consider IOIs in valid range
        if (ioi >= IOI_MIN && ioi < IOI_MAX) {
            int bin = (int)((ioi - IOI_MIN) / IOI_BIN_SIZE);
            if (bin >= 0 && bin < IOI_BINS) {
                histogram[bin]++;
                total_iois++;
            }
        }
    }
    
    if (total_iois < MIN_HISTOGRAM_COUNT) {
        *confidence = 0.0f;
        return 0.0f;
    }
    
    // Find peak bin (with smoothing - consider neighbors)
    int max_count = 0;
    int max_bin = 0;
    
    for (int i = 1; i < IOI_BINS - 1; i++) {
        // Smooth by summing with neighbors
        int smoothed = histogram[i - 1] + histogram[i] * 2 + histogram[i + 1];
        if (smoothed > max_count) {
            max_count = smoothed;
            max_bin = i;
        }
    }
    
    // Edge cases
    if (histogram[0] * 2 > max_count) {
        max_count = histogram[0] * 2;
        max_bin = 0;
    }
    if (histogram[IOI_BINS - 1] * 2 > max_count) {
        max_count = histogram[IOI_BINS - 1] * 2;
        max_bin = IOI_BINS - 1;
    }
    
    if (max_count < MIN_HISTOGRAM_COUNT * 2) {  // Account for smoothing factor
        *confidence = 0.0f;
        return 0.0f;
    }
    
    // Refine IOI estimate using weighted average around peak
    float weighted_sum = 0.0f;
    float weight_total = 0.0f;
    
    for (int i = (max_bin > 2 ? max_bin - 2 : 0); 
         i <= (max_bin < IOI_BINS - 3 ? max_bin + 2 : IOI_BINS - 1); i++) {
        float ioi_center = IOI_MIN + (i + 0.5f) * IOI_BIN_SIZE;
        float weight = (float)histogram[i];
        weighted_sum += ioi_center * weight;
        weight_total += weight;
    }
    
    float ioi = (weight_total > 0) ? (weighted_sum / weight_total) : (IOI_MIN + max_bin * IOI_BIN_SIZE);
    
    // Calculate confidence based on peak prominence
    *confidence = calculate_confidence(ctx, ioi);
    
    return ioi;
}

static float calculate_confidence(bpm_detector_ctx_t *ctx, float ioi) {
    if (ctx->onset_count < 10) return 0.3f;
    
    // Count how many IOIs are close to the detected beat period
    int matches = 0;
    float tolerance = ioi * 0.1f;  // 10% tolerance
    
    for (size_t i = 1; i < ctx->onset_count; i++) {
        float measured_ioi = ctx->onset_times[i] - ctx->onset_times[i - 1];
        
        // Check if IOI matches beat period or half/double (subbeats/half-time)
        if (fabsf(measured_ioi - ioi) < tolerance ||
            fabsf(measured_ioi - ioi * 2) < tolerance ||
            fabsf(measured_ioi - ioi / 2) < tolerance / 2) {
            matches++;
        }
    }
    
    float match_ratio = (float)matches / (ctx->onset_count - 1);
    
    // Scale to 0.4 - 0.95 range
    float confidence = 0.4f + match_ratio * 0.55f;
    if (confidence > 0.95f) confidence = 0.95f;
    
    return confidence;
}

bpm_result_t bpm_detector_get_result(bpm_detector_ctx_t *ctx) {
    bpm_result_t result = {0.0f, 0.0f};
    
    if (!ctx) return result;
    
    // Return cached result if valid
    if (ctx->result_valid) {
        return ctx->cached_result;
    }
    
    // Need at least 2 seconds of audio
    float duration = (float)ctx->sample_count / ctx->sample_rate;
    if (duration < 2.0f) {
        return result;
    }
    
    // Run analysis
    detect_onsets(ctx);
    
    float confidence;
    float ioi = find_dominant_ioi(ctx, &confidence);
    
    if (ioi <= 0.0f) {
        return result;
    }
    
    // Convert IOI to BPM
    float bpm = 60.0f / ioi;
    
    // Adjust to valid range by doubling or halving
    while (bpm < BPM_MIN && bpm > 0) bpm *= 2.0f;
    while (bpm > BPM_MAX) bpm /= 2.0f;
    
    result.bpm = bpm;
    result.confidence = confidence;
    
    // Cache result
    ctx->cached_result = result;
    ctx->result_valid = 1;
    
    return result;
}

bpm_result_t bpm_detector_finish(bpm_detector_ctx_t *ctx) {
    return bpm_detector_get_result(ctx);
}

void bpm_detector_destroy(bpm_detector_ctx_t *ctx) {
    if (!ctx) return;
    
    free(ctx->onset_times);
    free(ctx->energy_buffer);
    free(ctx->sample_buffer);
    free(ctx);
}

// Convenience function: detect BPM from a complete audio buffer
bpm_result_t bpm_detect(const int16_t *samples, size_t num_samples) {
    bpm_result_t result = {0.0f, 0.0f};
    
    if (!samples || num_samples < 44100 * 2) {  // Need at least 1 second stereo
        return result;
    }
    
    // Calculate duration (num_samples is total count for stereo)
    // Each stereo frame = 2 samples, so mono duration = num_samples / 2 / sample_rate
    uint32_t sample_rate = 44100;  // Assume standard sample rate
    uint32_t mono_samples = num_samples / 2;
    uint32_t duration_sec = (mono_samples / sample_rate) + 1;
    
    // Cap at 30 seconds for efficiency
    if (duration_sec > 30) duration_sec = 30;
    
    bpm_detector_ctx_t *ctx = bpm_detector_create(sample_rate, duration_sec);
    if (!ctx) return result;
    
    // Feed all samples (but respect the max duration)
    size_t max_stereo_samples = duration_sec * sample_rate * 2;
    size_t samples_to_feed = (num_samples < max_stereo_samples) ? num_samples : max_stereo_samples;
    
    bpm_detector_feed(ctx, samples, samples_to_feed);
    result = bpm_detector_finish(ctx);
    bpm_detector_destroy(ctx);
    
    return result;
}
