/**
 * @file ui_manager.c
 * @brief UI manager implementation
 */

#include "ui_manager.h"
#include "hud_theme.h"
#include "waveform_view.h"
#include "telemetry_view.h"
#include "crate_view.h"
#include "metadata_view.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_manager";

static bool s_initialized = false;
static ui_theme_t s_current_theme = UI_THEME_AMBER;
static ui_view_type_t s_current_view = UI_VIEW_WAVEFORM;
static uint32_t s_width = 0;
static uint32_t s_height = 0;

// Animation state for nudge effect
static bool nudge_animating = false;
static int nudge_offset = 0;

int ui_manager_init(uint32_t width, uint32_t height) {
    if (s_initialized) {
        ESP_LOGW(TAG, "UI already initialized");
        return 0;
    }
    
    s_width = width;
    s_height = height;
    
    ESP_LOGI(TAG, "Initializing UI manager: %ux%u", width, height);
    
    // Initialize LVGL driver
    ESP_LOGI(TAG, "Calling lvgl_driver_init...");
    int ret = lvgl_driver_init(width, height);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize LVGL driver");
        return ret;
    }
    ESP_LOGI(TAG, "LVGL driver initialized");
    
    // Initialize theme
    ESP_LOGI(TAG, "Initializing theme...");
    hud_theme_init(s_current_theme);
    ESP_LOGI(TAG, "Theme initialized");
    
    // Initialize views
    ESP_LOGI(TAG, "Initializing waveform view...");
    waveform_view_init(width, height);
    ESP_LOGI(TAG, "Waveform view initialized");
    
    ESP_LOGI(TAG, "Initializing telemetry view...");
    telemetry_view_init(width, height);
    ESP_LOGI(TAG, "Telemetry view initialized");
    
    ESP_LOGI(TAG, "Initializing crate view...");
    crate_view_init(width, height);
    ESP_LOGI(TAG, "Crate view initialized");
    
    ESP_LOGI(TAG, "Initializing metadata view...");
    metadata_view_init(width, height);
    ESP_LOGI(TAG, "Metadata view initialized");
    
    // Show initial view
    ESP_LOGI(TAG, "Setting initial view to WAVEFORM...");
    ui_manager_set_view(UI_VIEW_WAVEFORM);
    ESP_LOGI(TAG, "Initial view set");
    
    // Force a refresh to ensure everything is drawn
    lv_refr_now(NULL);
    ESP_LOGI(TAG, "Forced initial screen refresh");
    
    s_initialized = true;
    ESP_LOGI(TAG, "UI manager initialized successfully");
    
    return 0;
}

void ui_manager_deinit(void) {
    if (!s_initialized) return;
    
    lvgl_driver_deinit();
    
    s_initialized = false;
    ESP_LOGI(TAG, "UI manager deinitialized");
}

void ui_manager_set_theme(ui_theme_t theme) {
    if (theme >= 3) {
        theme = UI_THEME_AMBER; // Default
    }
    
    s_current_theme = theme;
    hud_theme_apply(theme);
    ESP_LOGI(TAG, "UI theme changed to %d", theme);
}

void ui_manager_set_view(ui_view_type_t view) {
    s_current_view = view;
    
    // Hide all views
    waveform_view_hide();
    crate_view_hide();
    
    // Show selected view
    switch (view) {
        case UI_VIEW_WAVEFORM:
            waveform_view_show();
            break;
        case UI_VIEW_CRATE:
            crate_view_show();
            break;
        case UI_VIEW_SETTINGS:
            // TODO: Show settings view
            ESP_LOGW(TAG, "Settings view not yet implemented");
            break;
    }
    
    ESP_LOGI(TAG, "UI view changed to %d", view);
}

void ui_manager_update_waveform(const uint8_t *waveform_data, 
                                size_t num_samples, 
                                float position) {
    if (!s_initialized) return;
    waveform_view_update(waveform_data, num_samples, position);
}

void ui_manager_update_telemetry(float bpm, float pitch, float phase_error) {
    if (!s_initialized) return;
    telemetry_view_update(bpm, pitch, phase_error);
}

void ui_manager_update_metadata(const char *title, const char *key, int32_t time_remaining) {
    if (!s_initialized) return;
    metadata_view_update(title, key, time_remaining);
}

void ui_manager_update_grid(const float *beat_positions, size_t num_beats) {
    if (!s_initialized) return;
    waveform_view_update_grid(beat_positions, num_beats);
}

void ui_manager_show_touch_feedback(float position, bool visible) {
    if (!s_initialized) return;
    waveform_view_show_cursor(position, visible);
}

void ui_manager_process(void) {
    if (!s_initialized) return;
    lvgl_driver_process();
}

void ui_manager_handle_touch(uint16_t x, uint16_t y, bool pressed) {
    if (!s_initialized) return;
    lvgl_driver_handle_touch(x, y, pressed);
}

/**
 * @brief Trigger nudge animation (waveform jerks left)
 */
void ui_manager_trigger_nudge_animation(void) {
    if (!s_initialized || nudge_animating) return;
    
    nudge_animating = true;
    nudge_offset = -10; // Jerk left 10px
    
    // TODO: Implement animation using LVGL animator
    // For now, this is a placeholder
    ESP_LOGI(TAG, "Nudge animation triggered");
    
    // Reset after animation
    nudge_offset = 0;
    nudge_animating = false;
}

