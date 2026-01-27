/**
 * @file telemetry_view.c
 * @brief Telemetry display (BPM, pitch, phase error)
 */

#include "telemetry_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "telemetry_view";

#define TELEMETRY_HEIGHT_PCT 30

static lv_obj_t *telemetry_container = NULL;
static lv_obj_t *bpm_label = NULL;
static lv_obj_t *pitch_label = NULL;
static lv_obj_t *phase_bar = NULL;
static lv_obj_t *phase_bar_bg = NULL;
static lv_obj_t *phase_indicator = NULL;

static uint32_t view_width = 0;
static uint32_t view_height = 0;
static float current_phase_error = 0.0f;

void telemetry_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Telemetry view initialized: %ux%u", width, height);
    
    view_width = width;
    view_height = 40; // Fixed height for bottom telemetry bar
    
    // Create container (bottom zone)
    telemetry_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(telemetry_container, view_width, view_height);
    lv_obj_set_pos(telemetry_container, 0, height - view_height);
    lv_obj_set_style_bg_color(telemetry_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(telemetry_container, 0, 0);
    lv_obj_set_style_pad_all(telemetry_container, 0, 0);
    lv_obj_clear_flag(telemetry_container, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_style_t *style_phosphor = hud_theme_get_phosphor_style();
    
    // BPM label (left side)
    bpm_label = lv_label_create(telemetry_container);
    // Use large font if available, otherwise default
    // lv_obj_set_style_text_font(bpm_label, &lv_font_montserrat_28, 0); 
    lv_obj_add_style(bpm_label, style_phosphor, 0);
    lv_label_set_text(bpm_label, "124.0");
    lv_obj_align(bpm_label, LV_ALIGN_LEFT_MID, 10, 0);
    
    // Pitch label (right side)
    pitch_label = lv_label_create(telemetry_container);
    lv_obj_add_style(pitch_label, style_phosphor, 0);
    lv_label_set_text(pitch_label, "+0.00%");
    lv_obj_align(pitch_label, LV_ALIGN_RIGHT_MID, -10, 0);
    
    // Phase error bar (center)
    int bar_width = 120;
    int bar_height = 6;
    
    // Background bar
    phase_bar_bg = lv_obj_create(telemetry_container);
    lv_obj_set_size(phase_bar_bg, bar_width, bar_height);
    lv_obj_align(phase_bar_bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(phase_bar_bg, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(phase_bar_bg, LV_OPA_30, 0); // Dim background
    lv_obj_set_style_border_width(phase_bar_bg, 1, 0);
    lv_obj_set_style_border_color(phase_bar_bg, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_radius(phase_bar_bg, 0, 0);
    lv_obj_clear_flag(phase_bar_bg, LV_OBJ_FLAG_CLICKABLE);
    
    // Phase indicator (center marker)
    phase_indicator = lv_obj_create(telemetry_container);
    lv_obj_set_size(phase_indicator, 2, bar_height + 6);
    lv_obj_align(phase_indicator, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(phase_indicator, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(phase_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(phase_indicator, 0, 0);
    lv_obj_clear_flag(phase_indicator, LV_OBJ_FLAG_CLICKABLE);
    
    // Phase error bar (dynamic fill)
    phase_bar = lv_obj_create(telemetry_container);
    lv_obj_set_size(phase_bar, 0, bar_height);
    lv_obj_align(phase_bar, LV_ALIGN_CENTER, 0, 0); // Will be re-positioned in update
    lv_obj_set_style_bg_color(phase_bar, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(phase_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(phase_bar, 0, 0);
    lv_obj_set_style_radius(phase_bar, 0, 0);
    lv_obj_clear_flag(phase_bar, LV_OBJ_FLAG_CLICKABLE);
    
    // Invalidate to trigger initial redraw
    lv_obj_invalidate(telemetry_container);
    ESP_LOGI(TAG, "Telemetry view created and invalidated");
}

void telemetry_view_update(float bpm, float pitch, float phase_error) {
    if (!telemetry_container) return;
    
    current_phase_error = phase_error;
    
    // Update BPM
    if (bpm_label) {
        char bpm_str[16];
        snprintf(bpm_str, sizeof(bpm_str), "%.1f", bpm);
        lv_label_set_text(bpm_label, bpm_str);
    }
    
    // Update pitch
    if (pitch_label) {
        char pitch_str[16];
        if (pitch >= 0) {
            snprintf(pitch_str, sizeof(pitch_str), "+%.2f%%", pitch);
        } else {
            snprintf(pitch_str, sizeof(pitch_str), "%.2f%%", pitch);
        }
        lv_label_set_text(pitch_label, pitch_str);
    }
    
    // Update phase error bar
    if (phase_bar && phase_bar_bg) {
        int bar_width = lv_obj_get_width(phase_bar_bg);
        int bar_x = lv_obj_get_x(phase_bar_bg);
        int bar_center_x = bar_x + bar_width / 2;
        
        // Clamp phase error to [-1.0, 1.0]
        if (phase_error < -1.0f) phase_error = -1.0f;
        if (phase_error > 1.0f) phase_error = 1.0f;
        
        // Calculate bar width and position
        int error_width = (int)(fabsf(phase_error) * bar_width / 2);
        int error_x;
        
        if (phase_error < 0) {
            // Behind: bar grows to the left
            error_x = bar_center_x - error_width;
        } else {
            // Ahead: bar grows to the right
            error_x = bar_center_x;
        }
        
        lv_obj_set_size(phase_bar, error_width, lv_obj_get_height(phase_bar));
        lv_obj_set_pos(phase_bar, error_x, lv_obj_get_y(phase_bar));
    }
}
