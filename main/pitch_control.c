/**
 * @file pitch_control.c
 * @brief Pitch/tempo control implementation with thread-safe atomic access
 * 
 * Provides pitch adjustment for real-time audio resampling.
 * Uses atomic operations to ensure thread safety between UI and audio tasks.
 */

#include "pitch_control.h"
#include "controls.h"
#include "esp_log.h"
#include <stdatomic.h>

static const char *TAG = "pitch_control";

// Atomic float storage for thread-safe access from audio task
// Uses atomic_int for storage since C11 doesn't guarantee atomic_float
static atomic_int current_pitch_int = ATOMIC_VAR_INIT(0);

static const float PITCH_MIN = -50.0f;
static const float PITCH_MAX = 50.0f;
static const float PITCH_STEP = 0.1f; // Per encoder click
static const float PITCH_SCALE = 1000.0f; // Convert float to int (3 decimal places)

bool pitch_control_init(void) {
    ESP_LOGI(TAG, "Initializing pitch control (atomic)");
    atomic_store(&current_pitch_int, 0);
    return true;
}

bool pitch_control_set(float pitch_percent) {
    if (pitch_percent < PITCH_MIN || pitch_percent > PITCH_MAX) {
        ESP_LOGE(TAG, "Pitch out of range: %.2f", pitch_percent);
        return false;
    }
    
    // Convert float to scaled int for atomic storage
    int scaled = (int)(pitch_percent * PITCH_SCALE);
    atomic_store(&current_pitch_int, scaled);
    ESP_LOGI(TAG, "Pitch set to %.2f%%", pitch_percent);
    return true;
}

float pitch_control_get(void) {
    // Atomic load - safe to call from audio ISR/task
    int scaled = atomic_load(&current_pitch_int);
    return (float)scaled / PITCH_SCALE;
}

void pitch_control_reset(void) {
    atomic_store(&current_pitch_int, 0);
    ESP_LOGI(TAG, "Pitch reset to 0%%");
}

// Update function to be called from main loop
void pitch_control_update(void) {
    int8_t delta = controls_get_pitch_delta();
    if (delta != 0) {
        float current = pitch_control_get();
        float new_pitch = current + (delta * PITCH_STEP);
        if (new_pitch >= PITCH_MIN && new_pitch <= PITCH_MAX) {
            pitch_control_set(new_pitch);
        }
    }
}

