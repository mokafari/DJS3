/**
 * @file echo.c
 * @brief Tempo-synced echo/delay effect implementation
 * 
 * Optimized for ESP32-S3 with:
 * - Circular buffer delay line
 * - Feedback with soft limiting to prevent runaway
 * - IRAM placement for hot processing loop
 */

#include "echo.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "echo";

/** Maximum feedback to prevent runaway (95%) */
#define MAX_FEEDBACK 0.95f

/** Beat division multipliers (fraction of one beat) */
static const float division_multipliers[ECHO_DIV_MAX] = {
    0.25f,  // ECHO_DIV_QUARTER
    0.50f,  // ECHO_DIV_HALF
    0.75f,  // ECHO_DIV_THREE_QUARTER
    1.00f   // ECHO_DIV_FULL
};

/**
 * @brief Calculate delay samples from BPM and division
 */
static void calculate_delay_samples(echo_t *echo) {
    if (echo->bpm <= 0.0f) {
        echo->delay_samples = 0;
        return;
    }
    
    // Seconds per beat = 60 / BPM
    float seconds_per_beat = 60.0f / echo->bpm;
    
    // Apply division multiplier
    float delay_seconds = seconds_per_beat * division_multipliers[echo->division];
    
    // Convert to samples (stereo frames)
    size_t delay = (size_t)(delay_seconds * (float)echo->sample_rate);
    
    // Clamp to buffer size (leave at least 1 sample headroom)
    if (delay >= echo->buffer_size) {
        delay = echo->buffer_size - 1;
    }
    
    echo->delay_samples = delay;
    
    ESP_LOGD(TAG, "Delay: %.1f BPM, div=%d -> %zu samples (%.1f ms)",
             echo->bpm, echo->division, delay,
             (float)delay * 1000.0f / (float)echo->sample_rate);
}

bool echo_init(echo_t *echo, uint32_t sample_rate, uint32_t max_delay_ms) {
    if (!echo || sample_rate == 0 || max_delay_ms == 0) {
        return false;
    }
    
    memset(echo, 0, sizeof(echo_t));
    
    // Calculate buffer size from max delay
    // Buffer holds stereo interleaved samples (2 int16 per frame)
    size_t max_delay_frames = (size_t)((uint64_t)sample_rate * max_delay_ms / 1000);
    size_t buffer_bytes = max_delay_frames * 2 * sizeof(int16_t);
    
    // Allocate delay buffer
    echo->buffer = (int16_t *)calloc(max_delay_frames * 2, sizeof(int16_t));
    if (!echo->buffer) {
        ESP_LOGE(TAG, "Failed to allocate delay buffer (%zu bytes)", buffer_bytes);
        return false;
    }
    
    echo->buffer_size = max_delay_frames;
    echo->write_pos = 0;
    echo->sample_rate = sample_rate;
    
    // Set defaults
    echo->bpm = 120.0f;
    echo->division = ECHO_DIV_QUARTER;
    echo->feedback = 0.5f;
    echo->mix = 0.5f;
    echo->enabled = true;
    
    calculate_delay_samples(echo);
    
    ESP_LOGI(TAG, "Echo initialized: %lu Hz, max %.1f ms (%zu frames, %zu KB)",
             (unsigned long)sample_rate, 
             (float)max_delay_ms,
             max_delay_frames,
             buffer_bytes / 1024);
    
    return true;
}

void echo_deinit(echo_t *echo) {
    if (echo) {
        if (echo->buffer) {
            free(echo->buffer);
            echo->buffer = NULL;
        }
        memset(echo, 0, sizeof(echo_t));
        ESP_LOGI(TAG, "Echo deinitialized");
    }
}

void echo_set_bpm(echo_t *echo, float bpm) {
    if (!echo) return;
    
    // Clamp to reasonable range
    if (bpm < 20.0f) bpm = 20.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    
    echo->bpm = bpm;
    calculate_delay_samples(echo);
}

void echo_set_delay_division(echo_t *echo, echo_division_t division) {
    if (!echo || division >= ECHO_DIV_MAX) return;
    
    echo->division = division;
    calculate_delay_samples(echo);
}

void echo_set_feedback(echo_t *echo, float feedback) {
    if (!echo) return;
    
    // Clamp to safe range [0, MAX_FEEDBACK]
    if (feedback < 0.0f) feedback = 0.0f;
    if (feedback > MAX_FEEDBACK) feedback = MAX_FEEDBACK;
    
    echo->feedback = feedback;
}

void echo_set_mix(echo_t *echo, float mix) {
    if (!echo) return;
    
    // Clamp to [0, 1]
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    
    echo->mix = mix;
}

void echo_set_enabled(echo_t *echo, bool enabled) {
    if (!echo) return;
    echo->enabled = enabled;
}

void echo_clear(echo_t *echo) {
    if (echo && echo->buffer) {
        memset(echo->buffer, 0, echo->buffer_size * 2 * sizeof(int16_t));
        echo->write_pos = 0;
    }
}

float echo_get_delay_ms(const echo_t *echo) {
    if (!echo || echo->sample_rate == 0) return 0.0f;
    return (float)echo->delay_samples * 1000.0f / (float)echo->sample_rate;
}

/**
 * @brief Soft clip to prevent overflow (simple saturation)
 */
static inline int32_t IRAM_ATTR soft_clip_i32(int32_t x) {
    if (x > 32000) return 32000 + ((x - 32000) >> 2);  // Soft knee above 32000
    if (x < -32000) return -32000 + ((x + 32000) >> 2);
    return x;
}

void IRAM_ATTR echo_process(echo_t *echo, int16_t *buffer, size_t samples) {
    // Early exit conditions
    if (!echo || !echo->enabled || !buffer || samples == 0) {
        return;
    }
    
    if (!echo->buffer || echo->delay_samples == 0) {
        return;
    }
    
    int16_t *delay_buf = echo->buffer;
    const size_t buf_size = echo->buffer_size;
    const size_t delay = echo->delay_samples;
    size_t write_pos = echo->write_pos;
    
    // Convert mix/feedback to fixed-point for faster processing
    // Use 8-bit fraction (256 = 1.0)
    const int32_t mix_wet = (int32_t)(echo->mix * 256.0f);
    const int32_t mix_dry = 256 - mix_wet;
    const int32_t feedback = (int32_t)(echo->feedback * 256.0f);
    
    for (size_t i = 0; i < samples; i++) {
        // Calculate read position (circular buffer)
        size_t read_pos = (write_pos + buf_size - delay) % buf_size;
        
        // Read input samples
        int32_t in_l = buffer[i * 2];
        int32_t in_r = buffer[i * 2 + 1];
        
        // Read delayed samples from buffer
        int32_t delayed_l = delay_buf[read_pos * 2];
        int32_t delayed_r = delay_buf[read_pos * 2 + 1];
        
        // Calculate feedback samples (input + delayed * feedback)
        int32_t fb_l = in_l + ((delayed_l * feedback) >> 8);
        int32_t fb_r = in_r + ((delayed_r * feedback) >> 8);
        
        // Soft clip to prevent runaway
        fb_l = soft_clip_i32(fb_l);
        fb_r = soft_clip_i32(fb_r);
        
        // Write to delay buffer (with feedback)
        delay_buf[write_pos * 2] = (int16_t)fb_l;
        delay_buf[write_pos * 2 + 1] = (int16_t)fb_r;
        
        // Mix dry and wet signals
        int32_t out_l = ((in_l * mix_dry) + (delayed_l * mix_wet)) >> 8;
        int32_t out_r = ((in_r * mix_dry) + (delayed_r * mix_wet)) >> 8;
        
        // Final clamp (shouldn't be needed normally)
        if (out_l > 32767) out_l = 32767;
        if (out_l < -32768) out_l = -32768;
        if (out_r > 32767) out_r = 32767;
        if (out_r < -32768) out_r = -32768;
        
        // Write output
        buffer[i * 2] = (int16_t)out_l;
        buffer[i * 2 + 1] = (int16_t)out_r;
        
        // Advance write position
        write_pos = (write_pos + 1) % buf_size;
    }
    
    // Save write position for next call
    echo->write_pos = write_pos;
}
