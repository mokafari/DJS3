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
#include "settings_view.h"
#include "search_view.h"
#include "performance_view.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "track_db.h"
#include "audio_player.h"
#include "preferences.h"
#include "track_history.h"
#include <string.h>

static const char *TAG = "ui_manager";

static bool s_initialized = false;
static ui_theme_t s_current_theme = UI_THEME_AMBER;
static ui_view_type_t s_current_view = UI_VIEW_WAVEFORM;
static uint32_t s_width = 0;
static uint32_t s_height = 0;
static bool s_search_mode = false;          // Search view overlay active
static bool s_performance_mode = false;      // Performance mode (minimal UI)

// Forward declarations for internal callbacks
static void on_search_track_selected(int track_index, void *user_data);
static void on_search_back(void *user_data);
static void on_settings_changed(settings_category_t category, int setting_id, int value, void *user_data);
static void on_settings_back(void *user_data);
static void apply_preferences_to_components(void);

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
    
    ESP_LOGI(TAG, "Initializing settings view...");
    settings_view_init(width, height);
    settings_view_set_changed_callback(on_settings_changed, NULL);
    settings_view_set_back_callback(on_settings_back, NULL);
    ESP_LOGI(TAG, "Settings view initialized");
    
    ESP_LOGI(TAG, "Initializing search view...");
    search_view_init(width, height);
    search_view_set_track_callback(on_search_track_selected, NULL);
    search_view_set_back_callback(on_search_back, NULL);
    ESP_LOGI(TAG, "Search view initialized");
    
    ESP_LOGI(TAG, "Initializing performance view...");
    performance_view_init(width, height);
    performance_view_set_exit_callback(ui_manager_exit_performance_mode);
    ESP_LOGI(TAG, "Performance view initialized");
    
    // Apply saved preferences to UI components
    apply_preferences_to_components();
    
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
            
            // Record track play in history
            if (!track_history_record_play(info.filename)) {
                ESP_LOGW(TAG, "Failed to record track in history");
            } else {
                ESP_LOGI(TAG, "Track recorded in history");
            }
            
            // Update search view with reference BPM and key for smart filtering
            if (info.bpm > 0) {
                search_view_set_reference_bpm(info.bpm);
            }
            if (info.key_id < 24) {
                search_view_set_reference_key(info.key_id);
            }
            
            // Immediately update metadata with new track info
            const char *key_str = (info.key_id < 24) ? audio_player_get_key_name() : "?";
            metadata_view_update(info.title, key_str, 0, 0);
            
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
    
    // Hide all views including search overlay
    waveform_view_hide();
    crate_view_hide();
    settings_view_hide();
    search_view_hide();
    s_search_mode = false;
    s_performance_mode = false;
    
    // Show selected view
    switch (view) {
        case UI_VIEW_WAVEFORM:
            waveform_view_show();
            break;
        case UI_VIEW_CRATE:
            crate_view_show();
            break;
        case UI_VIEW_SETTINGS:
            settings_view_show();
            break;
        case UI_VIEW_PERFORMANCE:
            // Performance mode uses waveform view with different styling
            waveform_view_show();
            s_performance_mode = true;
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
    
    // Update the appropriate view based on current mode
    if (s_performance_mode) {
        performance_view_update(waveform_data, num_samples, position, precise_time, wave_index);
    } else {
        waveform_view_update(waveform_data, num_samples, position, precise_time, wave_index);
    }
}

void ui_manager_reset_waveform(void) {
    if (!s_initialized) return;
    waveform_view_reset();
    performance_view_reset();
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

void ui_manager_enter_performance_mode(void) {
    if (!s_initialized) return;
    ui_manager_set_view(UI_VIEW_PERFORMANCE);
    ESP_LOGI(TAG, "Entered performance mode");
}

void ui_manager_exit_performance_mode(void) {
    if (!s_initialized) return;
    ui_manager_set_view(UI_VIEW_WAVEFORM);
    ESP_LOGI(TAG, "Exited performance mode");
}

bool ui_manager_is_performance_mode(void) {
    return s_performance_mode;
}

// ============================================================================
// Search View Integration
// ============================================================================

void ui_manager_show_search(void) {
    if (!s_initialized) return;
    
    // Show search view as overlay on crate view
    if (s_current_view == UI_VIEW_CRATE) {
        crate_view_hide();
    }
    search_view_show();
    s_search_mode = true;
    ESP_LOGI(TAG, "Search view shown");
}

void ui_manager_hide_search(void) {
    if (!s_initialized) return;
    
    search_view_hide();
    s_search_mode = false;
    
    // Restore crate view if that was the previous view
    if (s_current_view == UI_VIEW_CRATE) {
        crate_view_show();
    }
    ESP_LOGI(TAG, "Search view hidden");
}

bool ui_manager_is_search_visible(void) {
    return s_search_mode;
}

// ============================================================================
// Internal Callbacks
// ============================================================================

/**
 * @brief Callback when a track is selected from search results
 */
static void on_search_track_selected(int track_index, void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Search selected track index: %d", track_index);
    
    // Load the selected track (same as crate selection)
    ui_manager_handle_crate_select(track_index);
    
    // Hide search and switch to waveform view
    s_search_mode = false;
    search_view_hide();
}

/**
 * @brief Callback when user exits search view
 */
static void on_search_back(void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Search back requested");
    
    ui_manager_hide_search();
}

/**
 * @brief Callback for settings changes
 */
static void on_settings_changed(settings_category_t category, int setting_id, int value, void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Settings changed: category=%d, setting=%d, value=%d", category, setting_id, value);
    
    // Apply changes based on category and setting
    switch (category) {
        case SETTINGS_CAT_THEME:
            if (setting_id == 0) {  // Theme color
                ui_manager_set_theme((ui_theme_t)value);
                // Save to preferences
                if (prefs_is_initialized()) {
                    prefs_set_uint(PREFS_CAT_THEME, PREFS_KEY_THEME_STYLE, (uint32_t)value);
                }
            } else if (setting_id == 1) {  // Brightness
                // Update display brightness
                if (prefs_is_initialized()) {
                    prefs_set_brightness((uint8_t)value);
                }
            }
            break;
            
        case SETTINGS_CAT_AUDIO:
            if (setting_id == 0) {  // Audio mode
                // TODO: Switch between simple/granular audio modes
            } else if (setting_id == 1) {  // Volume
                if (prefs_is_initialized()) {
                    prefs_set_master_volume((int32_t)value);
                }
            }
            break;
            
        case SETTINGS_CAT_DISPLAY:
            if (setting_id == 0) {  // Waveform resolution
                ui_manager_set_waveform_resolution(value);
                if (prefs_is_initialized()) {
                    prefs_set_int(PREFS_CAT_DISPLAY, PREFS_KEY_DISP_WAVEFORM_ZOOM, value);
                }
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief Callback when user exits settings view
 */
static void on_settings_back(void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Settings back requested");
    
    // Save any pending preferences
    if (prefs_is_initialized()) {
        prefs_commit();
    }
    
    // Return to waveform view
    ui_manager_set_view(UI_VIEW_WAVEFORM);
}

/**
 * @brief Apply saved preferences to all UI components
 */
static void apply_preferences_to_components(void) {
    if (!prefs_is_initialized()) {
        ESP_LOGW(TAG, "Preferences not initialized, using defaults");
        return;
    }
    
    // Apply theme
    uint32_t theme_style = 0;
    prefs_get_uint(PREFS_CAT_THEME, PREFS_KEY_THEME_STYLE, &theme_style, PREFS_DEFAULT_THEME_STYLE);
    if (theme_style < 3) {
        s_current_theme = (ui_theme_t)theme_style;
        hud_theme_apply(s_current_theme);
    }
    
    // Apply waveform resolution
    int32_t waveform_zoom = 0;
    prefs_get_int(PREFS_CAT_DISPLAY, PREFS_KEY_DISP_WAVEFORM_ZOOM, &waveform_zoom, PREFS_DEFAULT_DISP_WAVEFORM_ZOOM);
    waveform_view_set_resolution(waveform_zoom);
    
    ESP_LOGI(TAG, "Preferences applied: theme=%lu, waveform_zoom=%ld", 
             (unsigned long)theme_style, (long)waveform_zoom);
}

