/**
 * @file synth_mode.c
 * @brief Synth mode control mapping implementation
 */

#include "synth_mode.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "synth_mode";

// Parameter ranges
#define GRAIN_SIZE_MIN_MS 10.0f
#define GRAIN_SIZE_MAX_MS 200.0f
#define DENSITY_MIN_PERCENT 25.0f
#define DENSITY_MAX_PERCENT 300.0f
#define JITTER_MAX_MS 50.0f

// Jitter decay rate (per sample at 44.1kHz)
#define JITTER_DECAY_RATE 0.0001f

int synth_mode_init(synth_mode_t *synth) {
    if (!synth) return -1;
    
    memset(synth, 0, sizeof(synth_mode_t));
    synth->active = false;
    synth->saved_params = granular_engine_default_params();
    synth->synth_params = granular_engine_default_params();
    
    ESP_LOGI(TAG, "Synth mode initialized");
    return 0;
}

void synth_mode_set_active(synth_mode_t *synth, bool enabled, 
                          const granular_params_t *current_params) {
    if (!synth) return;
    
    if (enabled && !synth->active) {
        // Entering synth mode: save current params
        if (current_params) {
            synth->saved_params = *current_params;
        }
        synth->synth_params = granular_engine_default_params();
        synth->active = true;
        ESP_LOGI(TAG, "Synth mode enabled");
    } else if (!enabled && synth->active) {
        // Exiting synth mode: restore saved params
        synth->active = false;
        ESP_LOGI(TAG, "Synth mode disabled");
    }
}

bool synth_mode_is_active(const synth_mode_t *synth) {
    if (!synth) return false;
    return synth->active;
}

float synth_mode_map_pitch_to_grain_size(const synth_mode_t *synth, float pitch_value) {
    if (!synth) return 50.0f;
    
    // Map pitch fader (-50 to +50) to grain size (10ms to 200ms)
    // Up (positive) = metallic/robot (small grains)
    // Down (negative) = chunk/glitch (large grains)
    float normalized = (pitch_value + 50.0f) / 100.0f; // 0.0 to 1.0
    float grain_size = GRAIN_SIZE_MIN_MS + 
                       (normalized * (GRAIN_SIZE_MAX_MS - GRAIN_SIZE_MIN_MS));
    
    // Invert: high pitch = small grain (metallic)
    grain_size = GRAIN_SIZE_MAX_MS - (grain_size - GRAIN_SIZE_MIN_MS);
    
    return fmaxf(GRAIN_SIZE_MIN_MS, fminf(GRAIN_SIZE_MAX_MS, grain_size));
}

float synth_mode_map_strip_to_density(const synth_mode_t *synth, float strip_position) {
    if (!synth) return 100.0f;
    
    // Map touch strip (0.0 to 1.0) to density (25% to 300%)
    // Left (0.0) = stutter/gated (low density)
    // Right (1.0) = lush/smear (high density)
    float density = DENSITY_MIN_PERCENT + 
                    (strip_position * (DENSITY_MAX_PERCENT - DENSITY_MIN_PERCENT));
    
    return fmaxf(DENSITY_MIN_PERCENT, fminf(DENSITY_MAX_PERCENT, density));
}

void synth_mode_trigger_jitter(synth_mode_t *synth, float jitter_amount) {
    if (!synth || !synth->active) return;
    
    // Set momentary jitter (will decay over time)
    synth->synth_params.jitter_ms = fmaxf(0.0f, fminf(JITTER_MAX_MS, jitter_amount));
    ESP_LOGI(TAG, "Jitter triggered: %.2f ms", jitter_amount);
}

void synth_mode_update_params(synth_mode_t *synth, 
                              float pitch_value, 
                              float strip_position,
                              granular_params_t *params) {
    if (!synth || !params) return;
    
    if (!synth->active) {
        // Not in synth mode, return saved params
        *params = synth->saved_params;
        return;
    }
    
    // Update synth params from controls
    synth->synth_params.grain_size_ms = synth_mode_map_pitch_to_grain_size(synth, pitch_value);
    synth->synth_params.density_percent = synth_mode_map_strip_to_density(synth, strip_position);
    
    // Decay jitter over time
    if (synth->synth_params.jitter_ms > 0.0f) {
        synth->synth_params.jitter_ms -= JITTER_DECAY_RATE * 1000.0f; // Approximate per-sample
        if (synth->synth_params.jitter_ms < 0.0f) {
            synth->synth_params.jitter_ms = 0.0f;
        }
    }
    
    *params = synth->synth_params;
}

granular_window_t synth_mode_cycle_window(synth_mode_t *synth) {
    if (!synth) return GRANULAR_WINDOW_HANN;
    
    // Cycle through window functions
    switch (synth->synth_params.window) {
        case GRANULAR_WINDOW_HANN:
            synth->synth_params.window = GRANULAR_WINDOW_HAMMING;
            break;
        case GRANULAR_WINDOW_HAMMING:
            synth->synth_params.window = GRANULAR_WINDOW_TRIANGLE;
            break;
        case GRANULAR_WINDOW_TRIANGLE:
            synth->synth_params.window = GRANULAR_WINDOW_RECTANGLE;
            break;
        case GRANULAR_WINDOW_RECTANGLE:
        default:
            synth->synth_params.window = GRANULAR_WINDOW_HANN;
            break;
    }
    
    ESP_LOGI(TAG, "Window function: %d", synth->synth_params.window);
    return synth->synth_params.window;
}

const granular_params_t* synth_mode_get_params(const synth_mode_t *synth) {
    if (!synth) return NULL;
    return &synth->synth_params;
}

