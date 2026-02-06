/**
 * @file echo.c
 * @brief Tempo-synced echo/delay effect implementation
 */

#include "echo.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "echo";

/** @brief Clamp float value to range */
static inline float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/** @brief Recalculate delay samples based on BPM and ratio */
static void echo_update_delay(echo_t *e) {
    // Calculate delay time in seconds: (60 / BPM) * ratio
    float delay_sec = (60.0f / e->bpm) * e->delay_ratio;
    
    // Convert to stereo sample pairs
    size_t delay = (size_t)(delay_sec * e->sample_rate) * 2;
    
    // Clamp to buffer size
    if (delay >= ECHO_MAX_DELAY_SAMPLES) {
        delay = ECHO_MAX_DELAY_SAMPLES - 2;
    }
    if (delay < 2) {
        delay = 2;
    }
    
    e->delay_samples = delay;
    
    ESP_LOGD(TAG, "Delay updated: %.1f BPM, ratio %.2f, %zu samples (%.1f ms)",
             e->bpm, e->delay_ratio, e->delay_samples,
             (float)e->delay_samples / (e->sample_rate * 2.0f) * 1000.0f);
}

void echo_init(echo_t *e, float sample_rate) {
    if (e == NULL) {
        ESP_LOGE(TAG, "NULL pointer passed to echo_init");
        return;
    }
    
    memset(e, 0, sizeof(echo_t));
    
    e->sample_rate = sample_rate;
    e->bpm = 120.0f;
    e->delay_ratio = 0.5f;  // Default to 1/2 beat
    e->feedback = 0.5f;
    e->mix = 0.5f;
    e->write_pos = 0;
    
    echo_update_delay(e);
    
    ESP_LOGI(TAG, "Echo initialized: %.0f Hz sample rate", sample_rate);
}

void echo_set_bpm(echo_t *e, float bpm) {
    if (e == NULL) return;
    
    e->bpm = clampf(bpm, 30.0f, 300.0f);
    echo_update_delay(e);
}

void echo_set_delay_ratio(echo_t *e, float ratio) {
    if (e == NULL) return;
    
    // Snap to valid ratios: 0.25, 0.5, 0.75, 1.0
    if (ratio <= 0.375f) {
        e->delay_ratio = 0.25f;
    } else if (ratio <= 0.625f) {
        e->delay_ratio = 0.5f;
    } else if (ratio <= 0.875f) {
        e->delay_ratio = 0.75f;
    } else {
        e->delay_ratio = 1.0f;
    }
    
    echo_update_delay(e);
}

void echo_set_feedback(echo_t *e, float feedback) {
    if (e == NULL) return;
    
    // Clamp to 0.0 - 0.95 to prevent runaway feedback
    e->feedback = clampf(feedback, 0.0f, 0.95f);
}

void echo_set_mix(echo_t *e, float wet_dry) {
    if (e == NULL) return;
    
    e->mix = clampf(wet_dry, 0.0f, 1.0f);
}

void echo_process(echo_t *e, int16_t *samples, size_t num_samples) {
    if (e == NULL || samples == NULL || num_samples == 0) {
        return;
    }
    
    const float feedback = e->feedback;
    const float wet = e->mix;
    const float dry = 1.0f - wet;
    const size_t delay = e->delay_samples;
    const size_t buf_size = ECHO_MAX_DELAY_SAMPLES;
    
    int16_t *buffer = e->buffer;
    size_t write_pos = e->write_pos;
    
    // Process stereo sample pairs
    for (size_t i = 0; i < num_samples * 2; i += 2) {
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
