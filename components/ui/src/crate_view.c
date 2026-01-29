/**
 * @file crate_view.c
 * @brief Library browser view with inverted selection
 */

#include "crate_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "ui_manager.h"
#include "track_db.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "crate_view";

static lv_obj_t *crate_container = NULL;
static lv_obj_t *track_list = NULL;
static lv_obj_t **track_items = NULL;
static char **track_names = NULL;  // Changed to char** to manage memory
static size_t num_tracks = 0;
static int selected_index = -1;
static bool visible = false;

static lv_style_t style_selected;
static lv_style_t style_normal;
static bool styles_initialized = false;

/**
 * @brief Initialize styles for crate view
 */
static void init_styles(void) {
    if (styles_initialized) return;
    
    // Get foreground color from theme
    lv_color_t fg_color = hud_theme_get_foreground_color();
    
    // Normal style (foreground color on black)
    lv_style_init(&style_normal);
    lv_style_set_text_color(&style_normal, fg_color);
    lv_style_set_bg_color(&style_normal, lv_color_black());
    lv_style_set_bg_opa(&style_normal, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_normal, 0);
    lv_style_set_pad_all(&style_normal, 4);
    
    // Selected style (black text on foreground color background) - inverted
    lv_style_init(&style_selected);
    lv_style_set_text_color(&style_selected, lv_color_black());
    lv_style_set_bg_color(&style_selected, fg_color);
    lv_style_set_bg_opa(&style_selected, LV_OPA_COVER);
    lv_style_set_border_width(&style_selected, 0);
    lv_style_set_pad_all(&style_selected, 4);
    
    styles_initialized = true;
}

/**
 * @brief Event handler for list item selection
 */
static void list_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        // Find which button was clicked
        for (size_t i = 0; i < num_tracks; i++) {
            if (track_items[i] == obj) {
                // Update selection
                if (selected_index >= 0 && selected_index < (int)num_tracks && track_items[selected_index]) {
                    lv_obj_remove_style(track_items[selected_index], &style_selected, 0);
                    lv_obj_add_style(track_items[selected_index], &style_normal, 0);
                }
                
                selected_index = (int)i;
                if (track_items[selected_index]) {
                    lv_obj_remove_style(track_items[selected_index], &style_normal, 0);
                    lv_obj_add_style(track_items[selected_index], &style_selected, 0);
                }
                
                // Notify UI manager to load track
                ui_manager_handle_crate_select(selected_index);
                break;
            }
        }
    }
}

void crate_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Crate view initialized: %ux%u", width, height);
    
    init_styles();
    
    // Create container (full screen, replaces waveform)
    crate_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(crate_container, width, height);
    lv_obj_set_pos(crate_container, 0, 0);
    lv_obj_set_style_bg_color(crate_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(crate_container, 0, 0);
    lv_obj_set_style_pad_all(crate_container, 0, 0);
    lv_obj_clear_flag(crate_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create list for tracks
    track_list = lv_list_create(crate_container);
    lv_obj_set_size(track_list, width, height);
    lv_obj_set_pos(track_list, 0, 0);
    lv_obj_set_style_bg_color(track_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(track_list, 0, 0);
    lv_obj_set_style_pad_all(track_list, 0, 0);
    
    // Remove event handler from list (we'll handle clicks on individual buttons)
    // lv_obj_add_event_cb(track_list, list_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Initially hidden
    lv_obj_add_flag(crate_container, LV_OBJ_FLAG_HIDDEN);
}

void crate_view_show(void) {
    visible = true;
    if (crate_container) {
        lv_obj_clear_flag(crate_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void crate_view_hide(void) {
    visible = false;
    if (crate_container) {
        lv_obj_add_flag(crate_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void crate_view_set_tracks(const char **tracks, size_t num_tracks_in) {
    if (!track_list) return;
    
    // Free old items and track names
    if (track_items) {
        for (size_t i = 0; i < num_tracks; i++) {
            if (track_items[i]) {
                lv_obj_del(track_items[i]);
            }
        }
        free(track_items);
        track_items = NULL;
    }
    
    // Free old track name strings
    if (track_names) {
        for (size_t i = 0; i < num_tracks; i++) {
            if (track_names[i]) {
                free(track_names[i]);
            }
        }
        free(track_names);
        track_names = NULL;
    }
    
    num_tracks = num_tracks_in;
    
    if (num_tracks == 0) {
        selected_index = -1;
        return;
    }
    
    // Allocate track names array
    track_names = (char**)calloc(num_tracks, sizeof(char*));
    if (!track_names) {
        ESP_LOGE(TAG, "Failed to allocate track names array");
        return;
    }
    
    // Copy track name strings
    for (size_t i = 0; i < num_tracks; i++) {
        if (tracks[i]) {
            track_names[i] = strdup(tracks[i]);
            if (!track_names[i]) {
                ESP_LOGE(TAG, "Failed to duplicate track name %zu", i);
                // Clean up on failure
                for (size_t j = 0; j < i; j++) {
                    free(track_names[j]);
                }
                free(track_names);
                track_names = NULL;
                return;
            }
        } else {
            track_names[i] = NULL;
        }
    }
    
    // Allocate item array
    track_items = (lv_obj_t**)calloc(num_tracks, sizeof(lv_obj_t*));
    if (!track_items) {
        ESP_LOGE(TAG, "Failed to allocate track items array");
        // Clean up track names
        for (size_t i = 0; i < num_tracks; i++) {
            free(track_names[i]);
        }
        free(track_names);
        track_names = NULL;
        return;
    }
    
    // Create list items
    for (size_t i = 0; i < num_tracks; i++) {
        if (track_names[i]) {
            track_items[i] = lv_list_add_btn(track_list, NULL, track_names[i]);
            lv_obj_add_style(track_items[i], &style_normal, 0);
            lv_obj_set_style_text_font(track_items[i], &lv_font_montserrat_14, 0);
            // Add click event to each button
            lv_obj_add_event_cb(track_items[i], list_event_handler, LV_EVENT_CLICKED, NULL);
        }
    }
    
    selected_index = -1;
}

void crate_view_set_selection(int index) {
    if (index < 0 || index >= (int)num_tracks || !track_items) {
        return;
    }
    
    // Remove selection from current item
    if (selected_index >= 0 && selected_index < (int)num_tracks && track_items[selected_index]) {
        lv_obj_remove_style(track_items[selected_index], &style_selected, 0);
        lv_obj_add_style(track_items[selected_index], &style_normal, 0);
    }
    
    // Apply selection to new item
    selected_index = index;
    if (track_items[selected_index]) {
        lv_obj_remove_style(track_items[selected_index], &style_normal, 0);
        lv_obj_add_style(track_items[selected_index], &style_selected, 0);
        
        // Scroll to selected item
        lv_obj_scroll_to_view(track_items[selected_index], LV_ANIM_ON);
    }
}

void crate_view_cleanup(void) {
    // Free track items
    if (track_items) {
        for (size_t i = 0; i < num_tracks; i++) {
            if (track_items[i]) {
                lv_obj_del(track_items[i]);
            }
        }
        free(track_items);
        track_items = NULL;
    }
    
    // Free track name strings
    if (track_names) {
        for (size_t i = 0; i < num_tracks; i++) {
            if (track_names[i]) {
                free(track_names[i]);
            }
        }
        free(track_names);
        track_names = NULL;
    }
    
    num_tracks = 0;
    selected_index = -1;
}

void crate_view_refresh_tracks(void) {
    if (!track_list) {
        ESP_LOGW(TAG, "Cannot refresh tracks: track_list not initialized");
        return;
    }
    
    // Get track count from database
    uint32_t track_count = track_db_get_count();
    ESP_LOGI(TAG, "Refreshing crate view with %lu tracks", track_count);
    
    if (track_count == 0) {
        crate_view_cleanup();
        return;
    }
    
    // Allocate array for track names
    const char **track_names_array = (const char**)malloc(track_count * sizeof(char*));
    if (!track_names_array) {
        ESP_LOGE(TAG, "Failed to allocate track names array for refresh");
        return;
    }
    
    // Get track info from database
    for (uint32_t i = 0; i < track_count; i++) {
        track_info_t info;
        if (track_db_get_track(i, &info)) {
            ESP_LOGI(TAG, "Track %lu: filename='%s', title='%s', has_id3=%d", 
                     i, info.filename, info.title, info.has_id3);
            
            // Use title if available, otherwise use filename
            const char *display_name = (info.title[0] != '\0') ? info.title : info.filename;
            track_names_array[i] = strdup(display_name);
            if (!track_names_array[i]) {
                ESP_LOGE(TAG, "Failed to duplicate track name %lu", i);
                // Clean up on failure
                for (uint32_t j = 0; j < i; j++) {
                    free((void*)track_names_array[j]);
                }
                free(track_names_array);
                return;
            }
            ESP_LOGD(TAG, "Track %lu display name: '%s'", i, track_names_array[i]);
        } else {
            ESP_LOGW(TAG, "Failed to get track %lu from database", i);
            track_names_array[i] = strdup("Unknown");
        }
    }
    
    // Update crate view with new tracks
    crate_view_set_tracks(track_names_array, track_count);
    ESP_LOGI(TAG, "Crate view updated with %lu tracks", track_count);
    
    // Free the temporary array (crate_view now owns the strings)
    free(track_names_array);
}
