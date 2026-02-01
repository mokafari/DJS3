/**
 * @file slip_loop.c
 * @brief Slip loop engine implementation
 */

#include "slip_loop.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_attr.h"  // For IRAM_ATTR
#include <string.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper: Linear interpolation (IRAM for cache-miss immunity)
static inline float IRAM_ATTR lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Helper: Random float between 0 and 1 (IRAM for cache-miss immunity)
static inline float IRAM_ATTR random_float(void) {
    return (float)rand() / (float)RAND_MAX;
}

static const char *TAG = "slip_loop";

// Default buffer size: 4 seconds @ 44.1kHz = ~176k samples
#define DEFAULT_BUFFER_SIZE_SAMPLES (44100 * 4)

int slip_loop_init(slip_loop_t *slip, size_t buffer_size_samples, uint32_t sample_rate) {
    if (!slip || buffer_size_samples == 0) {
        return -1;
    }

    memset(slip, 0, sizeof(slip_loop_t));
    
    // Allocate circular buffer in PSRAM with cache-line alignment for ESP32-S3
    size_t buffer_bytes = buffer_size_samples * sizeof(int16_t) * 2; // Stereo
    slip->circular_buffer = (int16_t *)heap_caps_aligned_alloc(
        32, buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!slip->circular_buffer) {
        // Fallback to internal RAM (aligned)
        slip->circular_buffer = (int16_t *)heap_caps_aligned_alloc(
            32, buffer_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!slip->circular_buffer) {
            ESP_LOGE(TAG, "Failed to allocate slip loop buffer");
            return -1;
        }
        ESP_LOGW(TAG, "Using internal RAM for slip loop buffer");
    }
    
    slip->buffer_size_samples = buffer_size_samples;
    slip->sample_rate = sample_rate;
    slip->mode = SLIP_LOOP_MODE_OFF;
    slip->playback_mode = SLIP_PLAYBACK_REGULAR;
    slip->is_active = false;
    slip->bpm = 120.0f;
    slip->reverse = false;
    slip->scatter = false;
    slip->scatter_probability = 0.1f;
    slip->base_length_ms = 0;
    slip->read_pos_frac = 0.0f;
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

void slip_loop_set_playback_mode(slip_loop_t *slip, slip_playback_mode_t playback_mode) {
    if (!slip) return;
    slip->playback_mode = playback_mode;
    if (playback_mode == SLIP_PLAYBACK_DJFX && slip->is_active) {
        // Store base length for pitch calculation
        slip->base_length_ms = slip->loop_length_ms;
    }
    ESP_LOGI(TAG, "Playback mode set: %d", playback_mode);
}

void slip_loop_set_reverse(slip_loop_t *slip, bool reverse) {
    if (!slip) return;
    slip->reverse = reverse;
    ESP_LOGI(TAG, "Reverse mode: %s", reverse ? "enabled" : "disabled");
}

void slip_loop_set_scatter(slip_loop_t *slip, bool scatter, float probability) {
    if (!slip) return;
    slip->scatter = scatter;
    slip->scatter_probability = fmaxf(0.0f, fminf(1.0f, probability));
    ESP_LOGI(TAG, "Scatter mode: %s (probability: %.2f)", 
             scatter ? "enabled" : "disabled", slip->scatter_probability);
}

void slip_loop_update_length(slip_loop_t *slip, uint32_t length_ms) {
    if (!slip || !slip->is_active) return;
    
    uint32_t length_samples = (length_ms * slip->sample_rate) / 1000;
    
    if (length_samples > slip->buffer_size_samples) {
        ESP_LOGW(TAG, "Requested length (%u samples) exceeds buffer size", length_samples);
        return;
    }
    
    slip->loop_length_ms = length_ms;
    slip->loop_end_pos = slip->loop_start_pos + length_samples;
    
    // Clamp read position to new loop length
    uint32_t loop_length = slip->loop_end_pos - slip->loop_start_pos;
    if (slip->loop_read_pos >= loop_length) {
        slip->loop_read_pos = loop_length - 1;
    }
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
    slip->base_length_ms = length_ms; // Store base length for DJFX
    slip->loop_start_pos = start_pos;
    slip->loop_end_pos = start_pos + length_samples;
    slip->loop_read_pos = 0;
    slip->read_pos_frac = 0.0f;
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
    slip->base_length_ms = length_ms; // Store base length for DJFX
    slip->loop_start_pos = start_pos;
    slip->loop_end_pos = start_pos + length_samples;
    slip->loop_read_pos = 0;
    slip->read_pos_frac = 0.0f;
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

void IRAM_ATTR slip_loop_process(slip_loop_t *slip,
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
        
        // Calculate pitch factor for DJFX mode
        float pitch_factor = 1.0f;
        if (slip->playback_mode == SLIP_PLAYBACK_DJFX && slip->base_length_ms > 0) {
            // Pitch factor: shorter loop = higher pitch, longer loop = lower pitch
            pitch_factor = (float)slip->base_length_ms / (float)slip->loop_length_ms;
            // Clamp to reasonable range (0.25x to 4x)
            pitch_factor = fmaxf(0.25f, fminf(4.0f, pitch_factor));
        }
        
        for (size_t i = 0; i < num_samples; i++) {
            // Scatter mode: random position jumps
            if (slip->scatter && random_float() < slip->scatter_probability) {
                slip->loop_read_pos = (uint32_t)(random_float() * loop_length_samples);
                slip->read_pos_frac = 0.0f;
            }
            
            // Calculate read position
            float read_pos = (float)slip->loop_read_pos + slip->read_pos_frac;
            
            // Handle loop boundaries
            if (read_pos >= loop_length_samples) {
                read_pos = fmodf(read_pos, loop_length_samples);
                slip->loop_read_pos = (uint32_t)read_pos;
                slip->read_pos_frac = read_pos - slip->loop_read_pos;
            } else if (read_pos < 0.0f) {
                read_pos = loop_length_samples + read_pos;
                slip->loop_read_pos = (uint32_t)read_pos;
                slip->read_pos_frac = read_pos - slip->loop_read_pos;
            }
            
            // Reverse mode: read backwards
            float actual_read_pos = read_pos;
            if (slip->reverse) {
                actual_read_pos = loop_length_samples - read_pos - 1.0f;
                if (actual_read_pos < 0.0f) actual_read_pos = 0.0f;
            }
            
            // Calculate position in circular buffer (stereo samples)
            uint32_t buffer_sample_idx = (slip->loop_start_pos + (uint32_t)actual_read_pos) % slip->buffer_size_samples;
            uint32_t buffer_stereo_idx = buffer_sample_idx * 2; // Stereo interleaved
            
            // DJFX mode: use fractional position for pitch shifting (linear interpolation)
            if (slip->playback_mode == SLIP_PLAYBACK_DJFX && pitch_factor != 1.0f) {
                float frac = actual_read_pos - (uint32_t)actual_read_pos;
                uint32_t idx0 = buffer_sample_idx;
                uint32_t idx1 = (idx0 + 1) % slip->buffer_size_samples;
                
                int16_t sample0_l = slip->circular_buffer[idx0 * 2];
                int16_t sample0_r = slip->circular_buffer[idx0 * 2 + 1];
                int16_t sample1_l = slip->circular_buffer[idx1 * 2];
                int16_t sample1_r = slip->circular_buffer[idx1 * 2 + 1];
                
                output[i * 2] = (int16_t)lerp((float)sample0_l, (float)sample1_l, frac);
                output[i * 2 + 1] = (int16_t)lerp((float)sample0_r, (float)sample1_r, frac);
            } else {
                // Regular mode: direct read
                output[i * 2] = slip->circular_buffer[buffer_stereo_idx];         // Left
                output[i * 2 + 1] = slip->circular_buffer[buffer_stereo_idx + 1]; // Right
            }
            
            // Advance read position
            if (slip->reverse) {
                slip->read_pos_frac -= pitch_factor;
                while (slip->read_pos_frac < 0.0f) {
                    slip->read_pos_frac += 1.0f;
                    if (slip->loop_read_pos == 0) {
                        slip->loop_read_pos = loop_length_samples - 1;
                    } else {
                        slip->loop_read_pos--;
                    }
                }
            } else {
                slip->read_pos_frac += pitch_factor;
                while (slip->read_pos_frac >= 1.0f) {
                    slip->read_pos_frac -= 1.0f;
                    slip->loop_read_pos++;
                    if (slip->loop_read_pos >= loop_length_samples) {
                        slip->loop_read_pos = 0;
                    }
                }
            }
            
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

