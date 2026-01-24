/**
 * @file pitch_control.c
 * @brief Pitch/tempo control implementation
 * 
 * Note: Actual audio resampling requires integration with audio processor
 */

#include "pitch_control.h"
#include "controls.h"
#include "esp_log.h"

static const char *TAG = "pitch_control";
static float current_pitch = 0.0f;
static const float PITCH_MIN = -50.0f;
static const float PITCH_MAX = 50.0f;
static const float PITCH_STEP = 0.1f; // Per encoder click

bool pitch_control_init(void) {
    ESP_LOGI(TAG, "Initializing pitch control");
    current_pitch = 0.0f;
    return true;
}

bool pitch_control_set(float pitch_percent) {
    if (pitch_percent < PITCH_MIN || pitch_percent > PITCH_MAX) {
        ESP_LOGE(TAG, "Pitch out of range: %.2f", pitch_percent);
        return false;
    }
    
    current_pitch = pitch_percent;
    ESP_LOGI(TAG, "Pitch set to %.2f%%", current_pitch);
    return true;
}

float pitch_control_get(void) {
    return current_pitch;
}

void pitch_control_reset(void) {
    current_pitch = 0.0f;
    ESP_LOGI(TAG, "Pitch reset to 0%%");
}

// Update function to be called from main loop
void pitch_control_update(void) {
    int8_t delta = controls_get_pitch_delta();
    if (delta != 0) {
        float new_pitch = current_pitch + (delta * PITCH_STEP);
        if (new_pitch >= PITCH_MIN && new_pitch <= PITCH_MAX) {
            pitch_control_set(new_pitch);
        }
    }
}

