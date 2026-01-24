/**
 * @file metadata_view.c
 * @brief Metadata display (top zone: title, key, time)
 */

#include "metadata_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "metadata_view";

#define METADATA_HEIGHT_PCT 20
#define TITLE_SCROLL_SPEED 2

static lv_obj_t *metadata_container = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *key_label = NULL;
static lv_obj_t *time_label = NULL;

static uint32_t view_width = 0;
static uint32_t view_height = 0;
static char title_buffer[256] = {0};
static int title_scroll_pos = 0;
static int title_scroll_delay = 0;

void metadata_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Metadata view initialized: %ux%u", width, height);
    
    view_width = width;
    view_height = height * METADATA_HEIGHT_PCT / 100;
    
    // Create container (top zone)
    metadata_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(metadata_container, view_width, view_height);
    lv_obj_set_pos(metadata_container, 0, 0);
    lv_obj_set_style_bg_color(metadata_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(metadata_container, 0, 0);
    lv_obj_set_style_pad_all(metadata_container, 0, 0);
    lv_obj_clear_flag(metadata_container, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_style_t *style_phosphor = hud_theme_get_phosphor_style();
    
    // Track title (scrolling marquee, left side)
    title_label = lv_label_create(metadata_container);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
    lv_obj_add_style(title_label, style_phosphor, 0);
    lv_label_set_text(title_label, "Track Title");
    lv_obj_set_pos(title_label, 10, view_height / 2 - 9);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(title_label, view_width * 60 / 100);
    
    // Key (Camelot notation, center)
    key_label = lv_label_create(metadata_container);
    lv_obj_set_style_text_font(key_label, &lv_font_montserrat_24, 0);
    lv_obj_add_style(key_label, style_phosphor, 0);
    lv_label_set_text(key_label, "4A");
    lv_obj_set_pos(key_label, view_width / 2 - 20, view_height / 2 - 12);
    lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Time remaining (right side)
    time_label = lv_label_create(metadata_container);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_18, 0);
    lv_obj_add_style(time_label, style_phosphor, 0);
    lv_label_set_text(time_label, "-03:45");
    lv_obj_set_pos(time_label, view_width - 100, view_height / 2 - 9);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
}

void metadata_view_update(const char *title, const char *key, int32_t time_remaining) {
    if (!metadata_container) return;
    
    // Update title
    if (title_label && title) {
        strncpy(title_buffer, title, sizeof(title_buffer) - 1);
        title_buffer[sizeof(title_buffer) - 1] = '\0';
        lv_label_set_text(title_label, title_buffer);
    }
    
    // Update key
    if (key_label && key) {
        lv_label_set_text(key_label, key);
    }
    
    // Update time remaining
    if (time_label) {
        char time_str[16];
        int32_t abs_time = abs(time_remaining);
        int minutes = abs_time / 60;
        int seconds = abs_time % 60;
        
        if (time_remaining < 0) {
            snprintf(time_str, sizeof(time_str), "-%02d:%02d", minutes, seconds);
        } else {
            snprintf(time_str, sizeof(time_str), "+%02d:%02d", minutes, seconds);
        }
        
        lv_label_set_text(time_label, time_str);
    }
}

