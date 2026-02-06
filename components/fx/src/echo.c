/**
 * @file echo.c
 * @brief Tempo-synced echo/delay effect implementation
 */

#include "echo.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "echo";

/**
 * @brief Maximum delay buffer size
 * 
 * Sized for 1 beat at 60 BPM @ 44100 Hz stereo = ~88200 samples
 * We use 96000 for headroom.
 */
#define ECHO_BUFFER_SIZE 96000

/** @brief Clamp float value to range */
static inline float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

float echo_division_to_ratio(echo_beat_division_t division) {
    switch (division) {
        case ECHO_BEAT_1_4: return 0.25f;
        case ECHO_BEAT_1_2: return 0.5f;
        case ECHO_BEAT_3_4: return 0.75f;
        case ECHO_BEAT_1:   return 1.0f;
        default:            return 0.5f;
    }
}

/** @brief Recalculate delay samples based on BPM and division */
static void echo_update_delay(echo_t *e) {
    if (!e->initialized || e->bpm < 30.0f) return;
    
    // Get ratio from beat division
    float ratio = echo_division_to_ratio(e->division);
    
    // Calculate delay time in seconds: (60 / BPM) * ratio
    float delay_sec = (60.0f / e->bpm) * ratio;
    
    // Convert to stereo sample pairs
    size_t delay = (size_t)(delay_sec * e->sample_rate) * 2;
    
    // Clamp to buffer size
    if (delay >= e->buffer_size) {
        delay = e->buffer_size - 2;
    }
    if (delay < 2) {
        delay = 2;
    }
    
    e->delay_samples = delay;
    
    ESP_LOGD(TAG, "Delay updated: %.1f BPM, division %d, %zu samples (%.1f ms)",
             e->bpm, e->division, e->delay_samples,
             (float)e->delay_samples / (e->sample_rate * 2.0f) * 1000.0f);
}

bool echo_init(echo_t *e) {
    if (e == NULL) {
        ESP_LOGE(TAG, "NULL pointer passed to echo_init");
        return false;
    }
    
    memset(e, 0, sizeof(echo_t));
    
    e->sample_rate = ECHO_DEFAULT_SAMPLE_RATE;
    e->bpm = 120.0f;
    e->division = ECHO_BEAT_1_2;  // Default to 1/2 beat
    e->feedback = 0.5f;
    e->mix = 0.5f;
    e->write_pos = 0;
    e->buffer_size = ECHO_BUFFER_SIZE;
    
    // Try to allocate buffer in PSRAM first, fall back to regular heap
    e->buffer = heap_caps_malloc(ECHO_BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (e->buffer == NULL) {
        e->buffer = malloc(ECHO_BUFFER_SIZE * sizeof(int16_t));
    }
    
    if (e->buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate delay buffer");
        return false;
    }
    
    memset(e->buffer, 0, ECHO_BUFFER_SIZE * sizeof(int16_t));
    
    e->initialized = true;
    echo_update_delay(e);
    
    ESP_LOGI(TAG, "Echo initialized: %.0f Hz sample rate, buffer %zu samples",
             e->sample_rate, e->buffer_size);
    
    return true;
}

void echo_deinit(echo_t *e) {
    if (e == NULL) return;
    
    if (e->buffer != NULL) {
        free(e->buffer);
        e->buffer = NULL;
    }
    
    e->initialized = false;
    ESP_LOGI(TAG, "Echo deinitialized");
}

void echo_set_bpm(echo_t *e, float bpm) {
    if (e == NULL) return;
    
    e->bpm = clampf(bpm, 30.0f, 300.0f);
    echo_update_delay(e);
}

void echo_set_beat_division(echo_t *e, echo_beat_division_t division) {
    if (e == NULL) return;
    
    if (division < ECHO_BEAT_COUNT) {
        e->division = division;
        echo_update_delay(e);
    }
}

void echo_set_feedback(echo_t *e, float feedback) {
    if (e == NULL) return;
    
    // Clamp to 0.0 - 0.95 to prevent runaway feedback
    e->feedback = clampf(feedback, 0.0f, ECHO_MAX_FEEDBACK);
}

void echo_set_mix(echo_t *e, float mix) {
    if (e == NULL) return;
    
    e->mix = clampf(mix, 0.0f, 1.0f);
}

void echo_process(echo_t *e, int16_t *samples, size_t num_frames) {
    if (e == NULL || samples == NULL || num_frames == 0 || !e->initialized) {
        return;
    }
    
    const float feedback = e->feedback;
    const float wet = e->mix;
    const float dry = 1.0f - wet;
    const size_t delay = e->delay_samples;
    const size_t buf_size = e->buffer_size;
    
    int16_t *buffer = e->buffer;
    size_t write_pos = e->write_pos;
    
    // Process stereo sample pairs (num_frames = number of stereo frames)
    for (size_t i = 0; i < num_frames * 2; i += 2) {
        // Calculate read position (behind write position by delay amount)
        size_t read_pos = (write_pos + buf_size - delay) % buf_size;
        
        // Read delayed samples
        int32_t delayed_l = buffer[read_pos];
        int32_t delayed_r = buffer[read_pos + 1];
        
        // Get input samples
        int32_t in_l = samples[i];
        int32_t in_r = samples[i + 1];
        
        // Calculate new buffer values: input + (delayed * feedback)
        int32_t buf_l = in_l + (int32_t)(delayed_l * feedback);
        int32_t buf_r = in_r + (int32_t)(delayed_r * feedback);
        
        // Soft clip to prevent overflow
        if (buf_l > 32767) buf_l = 32767;
        else if (buf_l < -32768) buf_l = -32768;
        if (buf_r > 32767) buf_r = 32767;
        else if (buf_r < -32768) buf_r = -32768;
        
        // Write to delay buffer
        buffer[write_pos] = (int16_t)buf_l;
        buffer[write_pos + 1] = (int16_t)buf_r;
        
        // Mix dry and wet signals
        int32_t out_l = (int32_t)(in_l * dry + delayed_l * wet);
        int32_t out_r = (int32_t)(in_r * dry + delayed_r * wet);
        
        // Clip output
        if (out_l > 32767) out_l = 32767;
        else if (out_l < -32768) out_l = -32768;
        if (out_r > 32767) out_r = 32767;
        else if (out_r < -32768) out_r = -32768;
        
        // Write output
        samples[i] = (int16_t)out_l;
        samples[i + 1] = (int16_t)out_r;
        
        // Advance write position
        write_pos = (write_pos + 2) % buf_size;
    }
    
    e->write_pos = write_pos;
}

void echo_clear(echo_t *e) {
    if (e == NULL || e->buffer == NULL) return;
    
    memset(e->buffer, 0, e->buffer_size * sizeof(int16_t));
    e->write_pos = 0;
    
    ESP_LOGD(TAG, "Delay buffer cleared");
}

float echo_get_delay_ms(const echo_t *e) {
    if (e == NULL || e->sample_rate < 1.0f) return 0.0f;
    
    // delay_samples is in stereo pairs, so divide by 2 for mono samples
    return (float)(e->delay_samples / 2) / e->sample_rate * 1000.0f;
}
