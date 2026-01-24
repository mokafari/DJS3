/**
 * @file crate_view.c
 * @brief Library browser view with inverted selection
 */

#include "crate_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "crate_view";

static lv_obj_t *crate_container = NULL;
static lv_obj_t *track_list = NULL;
static lv_obj_t **track_items = NULL;
static const char **track_names = NULL;
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
    
    // Free old items
    if (track_items) {
        for (size_t i = 0; i < num_tracks; i++) {
            if (track_items[i]) {
                lv_obj_del(track_items[i]);
            }
        }
        free(track_items);
        track_items = NULL;
    }
    
    num_tracks = num_tracks_in;
    track_names = tracks;
    
    if (num_tracks == 0) {
        return;
    }
    
    // Allocate item array
    track_items = (lv_obj_t**)calloc(num_tracks, sizeof(lv_obj_t*));
    if (!track_items) {
        ESP_LOGE(TAG, "Failed to allocate track items array");
        return;
    }
    
    // Create list items
    for (size_t i = 0; i < num_tracks; i++) {
        if (tracks[i]) {
            track_items[i] = lv_list_add_btn(track_list, NULL, tracks[i]);
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
