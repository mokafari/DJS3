/**
 * @file loop_control.c
 * @brief Loop control implementation
 */

#include "loop_control.h"
#include "esp_log.h"

static const char *TAG = "loop_control";
static uint32_t loop_in = 0;
static uint32_t loop_out = 0;
static bool loop_active = false;

bool loop_control_set_in(uint32_t position) {
    loop_in = position;
    if (loop_out > 0 && loop_out <= loop_in) {
        ESP_LOGW(TAG, "Loop out must be after loop in");
        return false;
    }
    loop_active = (loop_in > 0 && loop_out > 0);
    ESP_LOGI(TAG, "Loop in set at %d seconds", position);
    return true;
}

bool loop_control_set_out(uint32_t position) {
    loop_out = position;
    if (loop_in > 0 && loop_out <= loop_in) {
        ESP_LOGW(TAG, "Loop out must be after loop in");
        return false;
    }
    loop_active = (loop_in > 0 && loop_out > 0);
    ESP_LOGI(TAG, "Loop out set at %d seconds", position);
    return true;
}

uint32_t loop_control_get_in(void) {
    return loop_in;
}

uint32_t loop_control_get_out(void) {
    return loop_out;
}

uint32_t loop_control_get_length(void) {
    if (!loop_active) {
        return 0;
    }
    return loop_out - loop_in;
}

bool loop_control_is_active(void) {
    return loop_active;
}

void loop_control_clear(void) {
    loop_in = 0;
    loop_out = 0;
    loop_active = false;
    ESP_LOGI(TAG, "Loop cleared");
}

