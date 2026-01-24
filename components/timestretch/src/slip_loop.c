/**
 * @file slip_loop.c
 * @brief Slip loop engine implementation
 */

#include "slip_loop.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "slip_loop";

// Default buffer size: 4 seconds @ 44.1kHz = ~176k samples
#define DEFAULT_BUFFER_SIZE_SAMPLES (44100 * 4)

int slip_loop_init(slip_loop_t *slip, size_t buffer_size_samples, uint32_t sample_rate) {
    if (!slip || buffer_size_samples == 0) {
        return -1;
    }

    memset(slip, 0, sizeof(slip_loop_t));
    
    // Allocate circular buffer in PSRAM if available
    size_t buffer_bytes = buffer_size_samples * sizeof(int16_t) * 2; // Stereo
    slip->circular_buffer = (int16_t *)heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM);
    
    if (!slip->circular_buffer) {
        // Fallback to internal RAM
        slip->circular_buffer = (int16_t *)malloc(buffer_bytes);
        if (!slip->circular_buffer) {
            ESP_LOGE(TAG, "Failed to allocate slip loop buffer");
            return -1;
        }
        ESP_LOGW(TAG, "Using internal RAM for slip loop buffer");
    }
    
    slip->buffer_size_samples = buffer_size_samples;
    slip->sample_rate = sample_rate;
    slip->mode = SLIP_LOOP_MODE_OFF;
    slip->is_active = false;
    slip->bpm = 120.0f;
    slip->buffer_filled = false;
    
    ESP_LOGI(TAG, "Slip loop initialized: %zu samples @ %u Hz", 
             buffer_size_samples, sample_rate);
    
    return 0;
}

void slip_loop_deinit(slip_loop_t *slip) {
    if (!slip) return;
    
    if (slip->circular_buffer) {
        free(slip->circular_buffer);
        slip->circular_buffer = NULL;
    }
    
    memset(slip, 0, sizeof(slip_loop_t));
    ESP_LOGI(TAG, "Slip loop deinitialized");
}

void slip_loop_set_bpm(slip_loop_t *slip, float bpm) {
    if (!slip) return;
    slip->bpm = fmaxf(60.0f, fminf(180.0f, bpm));
}

int slip_loop_start_time(slip_loop_t *slip, uint32_t start_pos, uint32_t length_ms) {
    if (!slip || length_ms == 0) {
        return -1;
    }
    
    // Calculate loop length in samples
    uint32_t length_samples = (length_ms * slip->sample_rate) / 1000;
    
    if (length_samples > slip->buffer_size_samples) {
        ESP_LOGE(TAG, "Loop length (%u samples) exceeds buffer size (%zu)", 
                 length_samples, slip->buffer_size_samples);
        return -1;
    }
    
    slip->mode = SLIP_LOOP_MODE_TIME;
    slip->loop_length_ms = length_ms;
    slip->loop_start_pos = start_pos;
    slip->loop_end_pos = start_pos + length_samples;
    slip->loop_read_pos = 0;
    slip->background_start_pos = start_pos;
    slip->background_pos = start_pos;
    slip->is_active = true;
    slip->loop_start_time_ms = esp_timer_get_time() / 1000;
    
    ESP_LOGI(TAG, "Slip loop started (time): %u ms @ pos %u", 
             length_ms, start_pos);
    
    return 0;
}

int slip_loop_start_beat(slip_loop_t *slip, uint32_t start_pos, uint32_t length_beats) {
    if (!slip || length_beats == 0) {
        return -1;
    }
    
    // Calculate loop length in samples from beats
    float ms_per_beat = (60.0f * 1000.0f) / slip->bpm;
    uint32_t length_ms = (uint32_t)(length_beats * ms_per_beat);
    uint32_t length_samples = (length_ms * slip->sample_rate) / 1000;
    
    if (length_samples > slip->buffer_size_samples) {
        ESP_LOGE(TAG, "Loop length (%u samples) exceeds buffer size (%zu)", 
                 length_samples, slip->buffer_size_samples);
        return -1;
    }
    
    slip->mode = SLIP_LOOP_MODE_BEAT;
    slip->loop_length_beats = length_beats;
    slip->loop_length_ms = length_ms;
    slip->loop_start_pos = start_pos;
    slip->loop_end_pos = start_pos + length_samples;
    slip->loop_read_pos = 0;
    slip->background_start_pos = start_pos;
    slip->background_pos = start_pos;
    slip->is_active = true;
    slip->loop_start_time_ms = esp_timer_get_time() / 1000;
    
    ESP_LOGI(TAG, "Slip loop started (beat): %u beats (%.1f ms) @ pos %u", 
             length_beats, ms_per_beat * length_beats, start_pos);
    
    return 0;
}

uint32_t slip_loop_stop(slip_loop_t *slip) {
    if (!slip || !slip->is_active) {
        return 0;
    }
    
    uint32_t background_pos = slip->background_pos;
    
    slip->is_active = false;
    slip->mode = SLIP_LOOP_MODE_OFF;
    
    ESP_LOGI(TAG, "Slip loop stopped, jumping to background pos %u", background_pos);
    
    return background_pos;
}

bool slip_loop_is_active(const slip_loop_t *slip) {
    if (!slip) return false;
    return slip->is_active;
}

void slip_loop_process(slip_loop_t *slip,
                      const int16_t *main_buffer,
                      size_t main_buffer_size,
                      int16_t *output,
                      size_t num_samples) {
    if (!slip || !main_buffer || !output || num_samples == 0) {
        return;
    }
    
    if (slip->is_active) {
        // SLIP MODE: Read from loop buffer
        uint32_t loop_length_samples = slip->loop_end_pos - slip->loop_start_pos;
        
        for (size_t i = 0; i < num_samples; i++) {
            // Check if we've reached the end of the loop
            if (slip->loop_read_pos >= loop_length_samples) {
                slip->loop_read_pos = 0; // Loop back to start
            }
            
            // Calculate position in circular buffer (stereo samples)
            uint32_t buffer_sample_idx = (slip->loop_start_pos + slip->loop_read_pos) % slip->buffer_size_samples;
            uint32_t buffer_stereo_idx = buffer_sample_idx * 2; // Stereo interleaved
            
            // Read stereo sample from circular buffer
            output[i * 2] = slip->circular_buffer[buffer_stereo_idx];         // Left
            output[i * 2 + 1] = slip->circular_buffer[buffer_stereo_idx + 1]; // Right
            
            slip->loop_read_pos++;
            
            // Update background position (track continues playing silently)
            slip->background_pos++;
            if (slip->background_pos >= main_buffer_size) {
                slip->background_pos = 0;
            }
        }
    } else {
        // RECORD MODE: Write to circular buffer while playing normally
        for (size_t i = 0; i < num_samples; i++) {
            // Calculate positions (stereo samples)
            uint32_t buffer_sample_idx = slip->write_pos % slip->buffer_size_samples;
            uint32_t buffer_stereo_idx = buffer_sample_idx * 2;
            uint32_t main_sample_idx = slip->write_pos % main_buffer_size;
            uint32_t main_stereo_idx = main_sample_idx * 2;
            
            // Write stereo sample to circular buffer
            slip->circular_buffer[buffer_stereo_idx] = main_buffer[main_stereo_idx];         // Left
            slip->circular_buffer[buffer_stereo_idx + 1] = main_buffer[main_stereo_idx + 1]; // Right
            
            // Copy to output (normal playback)
            output[i * 2] = main_buffer[main_stereo_idx];
            output[i * 2 + 1] = main_buffer[main_stereo_idx + 1];
            
            slip->write_pos++;
        }
        
        if (!slip->buffer_filled && slip->write_pos >= slip->buffer_size_samples) {
            slip->buffer_filled = true;
            ESP_LOGI(TAG, "Slip loop buffer filled");
        }
    }
}

float slip_loop_get_position(const slip_loop_t *slip) {
    if (!slip || !slip->is_active) {
        return 0.0f;
    }
    
    uint32_t loop_length_samples = slip->loop_end_pos - slip->loop_start_pos;
    if (loop_length_samples == 0) return 0.0f;
    
    return (float)slip->loop_read_pos / (float)loop_length_samples;
}

uint32_t slip_loop_get_background_pos(const slip_loop_t *slip) {
    if (!slip) return 0;
    return slip->background_pos;
}

