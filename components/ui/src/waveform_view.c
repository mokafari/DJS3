/**
 * @file waveform_view.c
 * @brief Waveform display view - Scrolling, optimized buffer access
 */

#include "waveform_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>
#include <math.h>

static const char *TAG = "waveform_view";

#define WAVEFORM_BARS 480
#define GHOST_FRAMES 3
#define GRID_DOT_SPACING 4

static lv_obj_t *waveform_container = NULL;
static lv_obj_t *waveform_canvas = NULL;
static lv_obj_t *playhead_line = NULL;
static lv_obj_t *cursor_line = NULL;
static lv_obj_t *grid_container = NULL;

static uint32_t view_width = 0;
static uint32_t view_height = 0;
static uint32_t waveform_height = 0;
static bool visible = false;

// Circular buffer for ghosting effect
static uint8_t *waveform_history[GHOST_FRAMES] = {NULL};
static int history_index = 0;

// Grid lines
static float *beat_positions = NULL;
static size_t num_beats = 0;

/**
 * @brief Draw waveform with scrolling effect - Optimized direct buffer access
 */
static void draw_waveform(const uint8_t *waveform_data, size_t num_samples, float position) {
    if (!waveform_canvas || !visible) return;
    
    // Get canvas buffer
    lv_img_dsc_t *canvas_img = lv_canvas_get_img(waveform_canvas);
    lv_color_t *buffer = (lv_color_t *)canvas_img->data;
    
    if (!buffer) return;
    
    // Fast clear with memset (assuming black is 0)
    // Note: lv_color_t might be 16-bit or 32-bit. 
    // If black is 0x0000, memset works.
    size_t buffer_size_bytes = view_width * view_height * sizeof(lv_color_t);
    memset(buffer, 0, buffer_size_bytes); 
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    int center_y = view_height / 2;
    int center_x = view_width / 2;
    
    // Draw scrolling waveform
    // The data in waveform_data[0..num_samples-1] represents the *latest* history.
    // waveform_data[num_samples-1] is the newest sample (at playhead).
    // waveform_data[0] is the oldest sample (far left).
    // We want to draw this history to the LEFT of the center playhead.
    
    if (waveform_data && num_samples > 0) {
        // Iterate backwards from the playhead (center_x) to the left
        for (int x = 0; x < center_x; x++) {
            // Map screen X to sample index
            // At x = center_x, we want sample_idx = num_samples - 1 (newest)
            // At x = center_x - 1, we want sample_idx = num_samples - 2
            // So: sample_idx = num_samples - 1 - (center_x - x)
            
            int offset_from_center = center_x - x;
            int sample_idx = (int)num_samples - 1 - offset_from_center;
            
            if (sample_idx >= 0 && sample_idx < (int)num_samples) {
                uint8_t peak = waveform_data[sample_idx];
                
                // Scale height
                int bar_height = (peak * waveform_height) / 255;
                if (bar_height < 1) bar_height = 1; // Min 1px
                if (bar_height > (int)waveform_height) bar_height = waveform_height;
                
                int y_start = center_y - (bar_height / 2);
                int y_end = center_y + (bar_height / 2);
                
                // Clamp Y
                if (y_start < 0) y_start = 0;
                if (y_end >= (int)view_height) y_end = view_height - 1;
                
                // Draw vertical line directly into buffer
                for (int y = y_start; y <= y_end; y++) {
                    buffer[y * view_width + x] = fg_color;
                }
            }
        }
    }
    
    // Invalidate canvas to trigger redraw
    lv_obj_invalidate(waveform_canvas);
}

static lv_obj_t *progress_bar_bg = NULL;
static lv_obj_t *progress_bar_cursor = NULL;

void waveform_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Waveform view initialized: %ux%u", width, height);
    
    view_width = width;
    
    // Calculate available height: Total - Top Bar (30) - Bottom Bar (40)
    int top_bar_height = 30;
    int bottom_bar_height = 40;
    int available_height = height - top_bar_height - bottom_bar_height;
    
    // Use 60% of available height for the actual waveform, centered
    view_height = available_height; 
    waveform_height = view_height * 60 / 100;
    
    // Allocate history buffers (not used in optimized mode, but kept for compatibility/fallback)
    // ... (history buffer allocation) ...
    for (int i = 0; i < GHOST_FRAMES; i++) {
        size_t history_size = WAVEFORM_BARS * sizeof(uint8_t);
        // Try INTERNAL RAM first
        waveform_history[i] = (uint8_t*)heap_caps_malloc(history_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!waveform_history[i]) {
            waveform_history[i] = (uint8_t*)heap_caps_malloc(history_size, MALLOC_CAP_8BIT);
        }
        if (waveform_history[i]) memset(waveform_history[i], 0, WAVEFORM_BARS);
    }
    
    // Create container
    waveform_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(waveform_container, view_width, view_height);
    lv_obj_set_pos(waveform_container, 0, top_bar_height); 
    lv_obj_set_style_bg_color(waveform_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(waveform_container, 0, 0);
    lv_obj_set_style_pad_all(waveform_container, 0, 0);
    lv_obj_clear_flag(waveform_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create canvas for waveform
    waveform_canvas = lv_canvas_create(waveform_container);
    
    size_t canvas_buf_size = view_width * view_height * sizeof(lv_color_t);
    ESP_LOGI(TAG, "Allocating canvas buffer: %zu bytes", canvas_buf_size);
    
    void *canvas_buf = NULL;
    
    // Try INTERNAL RAM first
    canvas_buf = heap_caps_malloc(canvas_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!canvas_buf) {
        ESP_LOGW(TAG, "Internal RAM allocation failed, trying PSRAM...");
        canvas_buf = heap_caps_malloc(canvas_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    
    // Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
    
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        lv_obj_del(waveform_canvas);
        waveform_canvas = NULL;
    } else {
        ESP_LOGI(TAG, "Canvas buffer allocated at %p", canvas_buf);
        lv_canvas_set_buffer(waveform_canvas, canvas_buf, view_width, view_height, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_size(waveform_canvas, view_width, view_height);
        lv_obj_set_pos(waveform_canvas, 0, 0);
        lv_canvas_fill_bg(waveform_canvas, lv_color_black(), LV_OPA_COVER);
    }
    
    // Create fixed playhead line (center)
    playhead_line = lv_obj_create(waveform_container);
    lv_obj_set_size(playhead_line, 2, view_height);
    lv_obj_set_pos(playhead_line, view_width / 2 - 1, 0);
    lv_obj_set_style_bg_color(playhead_line, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(playhead_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(playhead_line, 0, 0);
    lv_obj_clear_flag(playhead_line, LV_OBJ_FLAG_CLICKABLE);
    
    // Progress Bar (bottom 4px)
    progress_bar_bg = lv_obj_create(waveform_container);
    lv_obj_set_size(progress_bar_bg, view_width, 4);
    lv_obj_set_align(progress_bar_bg, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_color(progress_bar_bg, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(progress_bar_bg, 0, 0);
    
    progress_bar_cursor = lv_obj_create(progress_bar_bg);
    lv_obj_set_size(progress_bar_cursor, 40, 4); // Small cursor
    lv_obj_set_style_bg_color(progress_bar_cursor, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_border_width(progress_bar_cursor, 0, 0);
    
    // Initially hidden
    lv_obj_add_flag(waveform_container, LV_OBJ_FLAG_HIDDEN);
}

void waveform_view_update(const uint8_t *waveform_data, 
                         size_t num_samples, 
                         float position) {
    if (!visible || !waveform_canvas) return;
    
    // Update progress bar cursor
    if (progress_bar_cursor && progress_bar_bg) {
        // position is 0.0 to 1.0 (start to end of track)
        int max_x = view_width - 40; // width - cursor width
        int cursor_x = (int)(position * max_x);
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x > max_x) cursor_x = max_x;
        lv_obj_set_x(progress_bar_cursor, cursor_x);
    }
    
    // Draw waveform
    draw_waveform(waveform_data, num_samples, position);
}

void waveform_view_show(void) {
    visible = true;
    if (waveform_container) {
        lv_obj_clear_flag(waveform_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void waveform_view_hide(void) {
    visible = false;
    if (waveform_container) {
        lv_obj_add_flag(waveform_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void waveform_view_update_grid(const float *beat_positions_in, size_t num_beats_in) {
    if (beat_positions) {
        free(beat_positions);
        beat_positions = NULL;
    }
    num_beats = num_beats_in;
    if (num_beats > 0 && beat_positions_in) {
        beat_positions = (float*)malloc(num_beats * sizeof(float));
        if (beat_positions) memcpy(beat_positions, beat_positions_in, num_beats * sizeof(float));
    }
}

void waveform_view_show_cursor(float position, bool visible_cursor) {
    // Optional: Implement touch feedback here if needed
}