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
#include "track_db.h"
#include "audio_player.h"
#include <string.h>

static const char *TAG = "ui_manager";

static bool s_initialized = false;
static ui_theme_t s_current_theme = UI_THEME_AMBER;
static ui_view_type_t s_current_view = UI_VIEW_WAVEFORM;
static uint32_t s_width = 0;
static uint32_t s_height = 0;

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
    
    // Populate crate view with tracks from DB
    // Note: crate_view_set_tracks now copies the strings internally, so we can free them
    uint32_t track_count = track_db_get_count();
    if (track_count > 0) {
        const char **track_names = (const char**)malloc(track_count * sizeof(char*));
        if (track_names) {
            for (uint32_t i = 0; i < track_count; i++) {
                track_info_t info;
                if (track_db_get_track(i, &info)) {
                    track_names[i] = strdup(info.title);
                } else {
                    track_names[i] = strdup("Unknown");
                }
            }
            crate_view_set_tracks(track_names, track_count);
            
            // Free the temporary array (crate_view now owns the copied strings)
            for (uint32_t i = 0; i < track_count; i++) {
                free((void*)track_names[i]);
            }
            free(track_names);
        }
    }
    
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

void ui_manager_handle_crate_select(int index) {
    track_info_t info;
    if (track_db_get_track(index, &info)) {
        ESP_LOGI(TAG, "Loading track from UI: index=%d, filename='%s', title='%s', has_id3=%d", 
                 index, info.filename, info.title, info.has_id3);
        
        // Reset waveform state before loading new track
        waveform_view_reset();
        
        // Try to load the file
        if (audio_player_load(info.filename)) {
            ESP_LOGI(TAG, "Track loaded successfully, starting playback");
            
            // Immediately update metadata with new track info
            metadata_view_update(info.title, "4A", 0, 0);
            
            // Load overview waveform if metadata is available
            // Note: audio_player_load is async, so metadata may not be immediately available
            // The overview will be loaded in the main loop once metadata loads
            static uint8_t overview_buffer[480];
            if (audio_player_get_overview(overview_buffer, 480)) {
                waveform_view_set_overview(overview_buffer, 480);
                ESP_LOGI(TAG, "Overview waveform loaded from metadata");
            } else {
                ESP_LOGI(TAG, "No overview waveform available yet (will be generated)");
            }
            
            audio_player_play();
            // Switch back to waveform view after loading
            ui_manager_set_view(UI_VIEW_WAVEFORM);
        } else {
            ESP_LOGE(TAG, "Failed to load track: %s", info.filename);
        }
    } else {
        ESP_LOGE(TAG, "Failed to get track info for index %d", index);
    }
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
                                float position,
                                float precise_time,
                                size_t wave_index) {
    if (!s_initialized) return;
    waveform_view_update(waveform_data, num_samples, position, precise_time, wave_index);
}

void ui_manager_reset_waveform(void) {
    if (!s_initialized) return;
    waveform_view_reset();
}

void ui_manager_set_overview_waveform(const uint8_t *data, size_t size) {
    if (!s_initialized) return;
    waveform_view_set_overview(data, size);
}

void ui_manager_update_telemetry(float bpm, float pitch, float phase_error) {
    if (!s_initialized) return;
    telemetry_view_update(bpm, pitch, phase_error);
}

void ui_manager_update_metadata(const char *title, const char *key, uint32_t position, uint32_t duration) {
    if (!s_initialized) return;
    metadata_view_update(title, key, position, duration);
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
    if (!s_initialized) return;
    
    waveform_view_trigger_nudge();
    ESP_LOGI(TAG, "Nudge animation triggered");
}

void ui_manager_refresh_crate(void) {
    if (!s_initialized) return;
    crate_view_refresh_tracks();
}

void ui_manager_set_waveform_resolution(int divider) {
    waveform_view_set_resolution(divider);
}

int ui_manager_get_waveform_resolution(void) {
    return waveform_view_get_resolution();
}

