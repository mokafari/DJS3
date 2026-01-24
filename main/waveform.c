/**
 * @file waveform.c
 * @brief Real-time waveform visualization implementation
 * 
 * This module generates waveform visualization from audio samples using FFT
 * and renders it with playhead position and cue point markers.
 */

#include "waveform.h"
#include "display.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <math.h>

// Note: FFT implementation requires esp_dsp component
// For now, using simplified FFT or placeholder
// Full FFT can be added when esp_dsp is available

static const char *TAG = "waveform";

// Waveform data
static float *fft_input = NULL;
static float *fft_output = NULL;
static uint16_t *waveform_data = NULL; // Pre-computed waveform points
static uint32_t waveform_length = 0;
static uint32_t current_position = 0;
static uint32_t track_duration = 0;

// Cue point markers
#define MAX_CUE_MARKERS 8
typedef struct {
    uint32_t position;
    uint16_t color;
    bool active;
} cue_marker_t;

static cue_marker_t cue_markers[MAX_CUE_MARKERS];
static bool waveform_initialized = false;

// FFT configuration (placeholder - requires esp_dsp)
// static fft_config_t *fft_config = NULL;

/**
 * @brief Initialize FFT
 * Note: Simplified implementation without esp_dsp
 */
static bool waveform_init_fft(void) {
    // Allocate input/output buffers
    fft_input = (float*)heap_caps_malloc(WAVEFORM_SAMPLES * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_output = (float*)heap_caps_malloc(WAVEFORM_SAMPLES * sizeof(float), MALLOC_CAP_SPIRAM);
    
    if (!fft_input || !fft_output) {
        ESP_LOGE(TAG, "Failed to allocate FFT buffers");
        return false;
    }
    
    return true;
}

/**
 * @brief Compute waveform from audio samples using simplified analysis
 * Note: Full FFT requires esp_dsp component
 */
static void waveform_compute_fft(int16_t *samples, uint32_t num_samples) {
    if (!fft_input || !fft_output) return;
    
    // Simplified: Compute RMS (root mean square) for waveform visualization
    // This gives a simple amplitude-based waveform without FFT
    uint32_t samples_to_process = (num_samples < WAVEFORM_SAMPLES) ? num_samples : WAVEFORM_SAMPLES;
    
    // Downsample and compute RMS for each window
    uint32_t window_size = samples_to_process / (WAVEFORM_SAMPLES / 2);
    if (window_size == 0) window_size = 1;
    
    for (uint32_t i = 0; i < WAVEFORM_SAMPLES / 2; i++) {
        uint32_t start = i * window_size;
        uint32_t end = start + window_size;
        if (end > samples_to_process) end = samples_to_process;
        
        // Compute RMS
        float sum_sq = 0.0f;
        uint32_t count = 0;
        for (uint32_t j = start; j < end; j++) {
            float sample = (float)samples[j] / 32768.0f;
            sum_sq += sample * sample;
            count++;
        }
        
        fft_output[i] = (count > 0) ? sqrtf(sum_sq / count) : 0.0f;
    }
}

/**
 * @brief Update waveform data from FFT output
 */
static void waveform_update_data(void) {
    if (!waveform_data || !fft_output) return;
    
    // Downsample FFT output to waveform width
    uint32_t fft_bins = WAVEFORM_SAMPLES / 2;
    uint32_t waveform_width = WAVEFORM_WIDTH;
    
    for (uint32_t i = 0; i < waveform_width; i++) {
        uint32_t start_bin = (i * fft_bins) / waveform_width;
        uint32_t end_bin = ((i + 1) * fft_bins) / waveform_width;
        
        // Average magnitude in this range
        float sum = 0.0f;
        uint32_t count = 0;
        for (uint32_t j = start_bin; j < end_bin && j < fft_bins; j++) {
            sum += fft_output[j];
            count++;
        }
        
        float magnitude = (count > 0) ? (sum / count) : 0.0f;
        
        // Normalize and convert to display height
        magnitude = fminf(magnitude * 100.0f, 1.0f); // Scale factor
        uint16_t height = (uint16_t)(magnitude * WAVEFORM_HEIGHT);
        
        waveform_data[i] = height;
    }
    
    waveform_length = waveform_width;
}

bool waveform_init(void) {
    ESP_LOGI(TAG, "Initializing waveform system");
    
    // Initialize FFT
    if (!waveform_init_fft()) {
        ESP_LOGE(TAG, "Failed to initialize FFT");
        return false;
    }
    
    // Allocate waveform data buffer
    waveform_data = (uint16_t*)heap_caps_malloc(
        WAVEFORM_WIDTH * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM
    );
    
    if (!waveform_data) {
        ESP_LOGE(TAG, "Failed to allocate waveform data buffer");
        return false;
    }
    
    // Initialize cue markers
    memset(cue_markers, 0, sizeof(cue_markers));
    
    waveform_initialized = true;
    ESP_LOGI(TAG, "Waveform system initialized");
    
    return true;
}

void waveform_update(int16_t *audio_samples, uint32_t num_samples,
                     uint32_t position, uint32_t duration) {
    if (!waveform_initialized) return;
    
    current_position = position;
    track_duration = duration;
    
    // Compute FFT from audio samples
    waveform_compute_fft(audio_samples, num_samples);
    
    // Update waveform data
    waveform_update_data();
}

void waveform_render(int x, int y, int width, int height) {
    if (!waveform_initialized || !waveform_data) return;
    
    // Background
    display_fill_rect(x, y, width, height, 0x0000); // Black
    
    // Draw waveform
    if (waveform_length > 0) {
        uint32_t samples_to_draw = (waveform_length < (uint32_t)width) ? waveform_length : width;
        float x_scale = (float)width / samples_to_draw;
        
        for (uint32_t i = 0; i < samples_to_draw; i++) {
            uint16_t sample_height = waveform_data[i];
            if (sample_height > (uint16_t)height) {
                sample_height = height;
            }
            
            int x_pos = x + (int)(i * x_scale);
            int y_center = y + height / 2;
            
            // Draw vertical line (centered)
            int y_top = y_center - sample_height / 2;
            int y_bottom = y_center + sample_height / 2;
            
            display_draw_line(x_pos, y_top, x_pos, y_bottom, 0x07E0); // Green
        }
    }
    
    // Draw playhead
    if (track_duration > 0) {
        int playhead_x = x + (int)((float)current_position / track_duration * width);
        display_draw_line(playhead_x, y, playhead_x, y + height, 0xF800); // Red
    }
    
    // Draw cue markers
    for (int i = 0; i < MAX_CUE_MARKERS; i++) {
        if (cue_markers[i].active && track_duration > 0) {
            int marker_x = x + (int)((float)cue_markers[i].position / track_duration * width);
            display_draw_line(marker_x, y, marker_x, y + height, cue_markers[i].color);
        }
    }
}

void waveform_set_playhead(uint32_t position) {
    current_position = position;
}

void waveform_set_duration(uint32_t duration) {
    track_duration = duration;
}

void waveform_add_cue_marker(uint8_t cue_id, uint32_t position, uint16_t color) {
    if (cue_id >= MAX_CUE_MARKERS) return;
    
    cue_markers[cue_id].position = position;
    cue_markers[cue_id].color = color;
    cue_markers[cue_id].active = true;
}

void waveform_remove_cue_marker(uint8_t cue_id) {
    if (cue_id >= MAX_CUE_MARKERS) return;
    
    cue_markers[cue_id].active = false;
}

void waveform_clear_cue_markers(void) {
    memset(cue_markers, 0, sizeof(cue_markers));
}
