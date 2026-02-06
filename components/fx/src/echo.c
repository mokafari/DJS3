/**
 * @file echo.c
 * @brief Tempo-synced echo/delay effect implementation
 */

#include "echo.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "echo";

/**
 * @brief Beat fraction multipliers for each sync mode
 */
static const float SYNC_FRACTIONS[ECHO_SYNC_COUNT] = {
    0.25f,  // ECHO_SYNC_1_4 - quarter beat
    0.50f,  // ECHO_SYNC_1_2 - half beat
    0.75f,  // ECHO_SYNC_3_4 - three-quarter beat
    1.00f   // ECHO_SYNC_1_1 - full beat
};

/**
 * @brief Clamp float value to range
 */
static inline float clamp_f(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * @brief Clamp int32 to int16 range with saturation
 */
static inline int16_t sat_i16(int32_t val) {
    if (val > INT16_MAX) return INT16_MAX;
    if (val < INT16_MIN) return INT16_MIN;
    return (int16_t)val;
}

float echo_calc_delay_ms(float bpm, echo_sync_t sync) {
    if (bpm <= 0.0f) {
        return 0.0f;
    }
    
    // Clamp sync mode to valid range
    if (sync >= ECHO_SYNC_COUNT) {
        sync = ECHO_SYNC_1_1;
    }
    
    // delay_ms = (60000 / bpm) * beat_fraction
    // At 120 BPM: 1 beat = 500ms
    float beat_ms = 60000.0f / bpm;
    return beat_ms * SYNC_FRACTIONS[sync];
}

/**
 * @brief Update delay samples from current BPM and sync mode
 */
static void update_delay_samples(echo_t *echo) {
    float delay_ms = echo_calc_delay_ms(echo->bpm, echo->sync_mode);
    
    // Clamp to maximum delay
    if (delay_ms > ECHO_MAX_DELAY_MS) {
        delay_ms = ECHO_MAX_DELAY_MS;
    }
    
    // Convert ms to samples
    echo->delay_samples = (size_t)((delay_ms / 1000.0f) * echo->sample_rate);
    
    // Ensure delay doesn't exceed buffer size
    if (echo->delay_samples >= echo->buffer_size) {
        echo->delay_samples = echo->buffer_size - 1;
    }
    
    // Minimum delay of 1 sample
    if (echo->delay_samples < 1) {
        echo->delay_samples = 1;
    }
}

bool echo_init(echo_t *echo, uint32_t sample_rate) {
    if (!echo || sample_rate == 0) {
        return false;
    }
    
    // Clear structure
    memset(echo, 0, sizeof(echo_t));
    
    echo->sample_rate = sample_rate;
    
    // Calculate buffer size for max delay
    // buffer_size = (max_delay_ms / 1000) * sample_rate
    echo->buffer_size = (size_t)((ECHO_MAX_DELAY_MS / 1000.0f) * sample_rate) + 1;
    
    // Allocate delay buffers (prefer PSRAM if available for large buffers)
    // At 44100 Hz, 2 seconds = 88200 samples * 2 bytes = ~172KB per channel
    size_t buffer_bytes = echo->buffer_size * sizeof(int16_t);
    
    // Try PSRAM first, fall back to internal RAM
    echo->buffer_l = (int16_t *)heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!echo->buffer_l) {
        echo->buffer_l = (int16_t *)malloc(buffer_bytes);
    }
    
    echo->buffer_r = (int16_t *)heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!echo->buffer_r) {
        echo->buffer_r = (int16_t *)malloc(buffer_bytes);
    }
    
    if (!echo->buffer_l || !echo->buffer_r) {
        ESP_LOGE(TAG, "Failed to allocate delay buffers (%zu bytes each)", buffer_bytes);
        echo_deinit(echo);
        return false;
    }
    
    // Clear buffers
    memset(echo->buffer_l, 0, buffer_bytes);
    memset(echo->buffer_r, 0, buffer_bytes);
    
    // Set default parameters
    echo->write_pos = 0;
    echo->bpm = 120.0f;
    echo->sync_mode = ECHO_SYNC_1_2;
    echo->feedback = 0.5f;
    echo->mix = 0.5f;
    echo->enabled = false;
    
    // Calculate initial delay
    update_delay_samples(echo);
    
    ESP_LOGI(TAG, "Initialized: %lu Hz, buffer %zu samples (%.1f ms max)",
             (unsigned long)sample_rate, echo->buffer_size, (float)ECHO_MAX_DELAY_MS);
    
    return true;
}

void echo_deinit(echo_t *echo) {
    if (!echo) {
        return;
    }
    
    if (echo->buffer_l) {
        free(echo->buffer_l);
        echo->buffer_l = NULL;
    }
    
    if (echo->buffer_r) {
        free(echo->buffer_r);
        echo->buffer_r = NULL;
    }
    
    echo->buffer_size = 0;
}

void echo_set_bpm(echo_t *echo, float bpm) {
    if (!echo) {
        return;
    }
    
    // Clamp BPM to reasonable range
    echo->bpm = clamp_f(bpm, 30.0f, 300.0f);
    update_delay_samples(echo);
    
    ESP_LOGD(TAG, "BPM: %.1f, delay: %zu samples (%.1f ms)",
             echo->bpm, echo->delay_samples, echo_get_delay_ms(echo));
}

void echo_set_sync(echo_t *echo, echo_sync_t sync) {
    if (!echo || sync >= ECHO_SYNC_COUNT) {
        return;
    }
    
    echo->sync_mode = sync;
    update_delay_samples(echo);
    
    ESP_LOGD(TAG, "Sync mode: %d, delay: %zu samples (%.1f ms)",
             sync, echo->delay_samples, echo_get_delay_ms(echo));
}

void echo_set_feedback(echo_t *echo, float feedback) {
    if (!echo) {
        return;
    }
    
    // Clamp feedback to prevent runaway oscillation
    echo->feedback = clamp_f(feedback, 0.0f, 0.95f);
}

void echo_set_mix(echo_t *echo, float mix) {
    if (!echo) {
        return;
    }
    
    echo->mix = clamp_f(mix, 0.0f, 1.0f);
}

void echo_set_enabled(echo_t *echo, bool enabled) {
    if (!echo) {
        return;
    }
    
    echo->enabled = enabled;
    
    // Clear buffer when disabling to prevent stale audio on re-enable
    if (!enabled) {
        echo_reset(echo);
    }
}

void echo_set_delay_ms(echo_t *echo, float delay_ms) {
    if (!echo) {
        return;
    }
    
    // Clamp to valid range
    delay_ms = clamp_f(delay_ms, 0.0f, (float)ECHO_MAX_DELAY_MS);
    
    // Convert to samples
    echo->delay_samples = (size_t)((delay_ms / 1000.0f) * echo->sample_rate);
    
    if (echo->delay_samples >= echo->buffer_size) {
        echo->delay_samples = echo->buffer_size - 1;
    }
    if (echo->delay_samples < 1) {
        echo->delay_samples = 1;
    }
}

float echo_get_delay_ms(const echo_t *echo) {
    if (!echo || echo->sample_rate == 0) {
        return 0.0f;
    }
    
    return (float)echo->delay_samples * 1000.0f / (float)echo->sample_rate;
}

void echo_process(echo_t *echo, int16_t *buffer, size_t num_frames) {
    if (!echo || !buffer || num_frames == 0) {
        return;
    }
    
    // Bypass if disabled
    if (!echo->enabled) {
        return;
    }
    
    const float feedback = echo->feedback;
    const float mix = echo->mix;
    const float dry = 1.0f - mix;
    const size_t delay = echo->delay_samples;
    const size_t buf_size = echo->buffer_size;
    
    int16_t *buf_l = echo->buffer_l;
    int16_t *buf_r = echo->buffer_r;
    size_t write_pos = echo->write_pos;
    
    for (size_t i = 0; i < num_frames; i++) {
        // Get input samples (stereo interleaved)
        int32_t in_l = buffer[i * 2];
        int32_t in_r = buffer[i * 2 + 1];
        
        // Calculate read position (circular buffer)
        size_t read_pos = (write_pos + buf_size - delay) % buf_size;
        
        // Read delayed samples
        int32_t delayed_l = buf_l[read_pos];
        int32_t delayed_r = buf_r[read_pos];
        
        // Calculate output: dry * input + wet * delayed
        int32_t out_l = (int32_t)(dry * in_l + mix * delayed_l);
        int32_t out_r = (int32_t)(dry * in_r + mix * delayed_r);
        
        // Write to delay buffer: input + feedback * delayed
        // This creates the repeating echo effect
        int32_t write_l = in_l + (int32_t)(feedback * delayed_l);
        int32_t write_r = in_r + (int32_t)(feedback * delayed_r);
        
        // Saturate and write to buffer
        buf_l[write_pos] = sat_i16(write_l);
        buf_r[write_pos] = sat_i16(write_r);
        
        // Write output (saturated to prevent clipping)
        buffer[i * 2] = sat_i16(out_l);
        buffer[i * 2 + 1] = sat_i16(out_r);
        
        // Advance write position
        write_pos = (write_pos + 1) % buf_size;
    }
    
    // Save write position for next call
    echo->write_pos = write_pos;
}

void echo_reset(echo_t *echo) {
    if (!echo) {
        return;
    }
    
    // Clear delay buffers
    if (echo->buffer_l) {
        memset(echo->buffer_l, 0, echo->buffer_size * sizeof(int16_t));
    }
    if (echo->buffer_r) {
        memset(echo->buffer_r, 0, echo->buffer_size * sizeof(int16_t));
    }
    
    // Reset write position
    echo->write_pos = 0;
}
