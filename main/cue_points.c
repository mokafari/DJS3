/**
 * @file cue_points.c
 * @brief Cue point system implementation
 */

#include "cue_points.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cue_points";
static uint32_t cue_positions[MAX_CUE_POINTS] = {0};
static bool cue_set[MAX_CUE_POINTS] = {0};

bool cue_points_set(uint8_t cue_index, uint32_t position) {
    if (cue_index >= MAX_CUE_POINTS) {
        ESP_LOGE(TAG, "Invalid cue index: %d", cue_index);
        return false;
    }
    
    cue_positions[cue_index] = position;
    cue_set[cue_index] = true;
    ESP_LOGI(TAG, "Cue %d set at %d seconds", cue_index, position);
    return true;
}

uint32_t cue_points_get(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS || !cue_set[cue_index]) {
        return 0;
    }
    return cue_positions[cue_index];
}

void cue_points_clear(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        return;
    }
    cue_set[cue_index] = false;
    cue_positions[cue_index] = 0;
    ESP_LOGI(TAG, "Cue %d cleared", cue_index);
}

void cue_points_clear_all(void) {
    memset(cue_set, 0, sizeof(cue_set));
    memset(cue_positions, 0, sizeof(cue_positions));
    ESP_LOGI(TAG, "All cue points cleared");
}

