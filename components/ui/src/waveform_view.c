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

// Ring buffer scroll optimization
static size_t last_wave_index = 0;
static bool first_frame = true;
#define SCROLL_DELTA_THRESHOLD 20  // Full redraw if scroll > 20 pixels

// Grid lines
static float *beat_positions = NULL;
static size_t num_beats = 0;

/**
 * @brief Draw a single vertical bar at position x
 */
static inline void draw_bar_at(lv_color_t *buffer, int x, uint8_t peak, 
                                int center_y, lv_color_t fg_color, lv_color_t bg_color) {
    if (x < 0 || x >= (int)view_width) return;
    
    // Clear the column first
    for (int y = 0; y < (int)view_height; y++) {
        buffer[y * view_width + x] = bg_color;
    }
    
    if (peak > 0) {
        int bar_height = (peak * waveform_height) / 255;
        if (bar_height < 1) bar_height = 1;
        if (bar_height > (int)waveform_height) bar_height = waveform_height;
        
        int y_start = center_y - (bar_height / 2);
        int y_end = center_y + (bar_height / 2);
        
        if (y_start < 0) y_start = 0;
        if (y_end >= (int)view_height) y_end = view_height - 1;
        
        for (int y = y_start; y <= y_end; y++) {
            buffer[y * view_width + x] = fg_color;
        }
    }
}

/**
 * @brief Draw waveform with ring buffer scroll optimization
 * 
 * Uses incremental scrolling: shifts existing pixels left and only draws
 * new columns. Falls back to full redraw on seek/scrub (backwards or large jump).
 */
static void draw_waveform(const uint8_t *waveform_data, size_t num_samples, size_t wave_index) {
    if (!waveform_canvas || !visible) return;
    
    lv_img_dsc_t *canvas_img = lv_canvas_get_img(waveform_canvas);
    lv_color_t *buffer = (lv_color_t *)canvas_img->data;
    if (!buffer) return;
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    lv_color_t bg_color = lv_color_black();
    int center_y = view_height / 2;
    
    // Calculate scroll delta since last frame
    int scroll_delta = 0;
    bool need_full_redraw = first_frame;
    
    if (!first_frame) {
        scroll_delta = (int)wave_index - (int)last_wave_index;
        
        // Full redraw if: backwards scroll (scrubbing) OR large jump (seek)
        if (scroll_delta < 0 || scroll_delta > SCROLL_DELTA_THRESHOLD) {
            need_full_redraw = true;
        }
    }
    
    first_frame = false;
    last_wave_index = wave_index;
    
    if (need_full_redraw) {
        // Full redraw: clear and draw all columns
        size_t buffer_size_bytes = view_width * view_height * sizeof(lv_color_t);
        memset(buffer, 0, buffer_size_bytes);
        
        if (waveform_data && num_samples > 0) {
            for (int x = 0; x < (int)view_width && x < (int)num_samples; x++) {
                uint8_t peak = waveform_data[x];
                if (peak > 0) {
                    int bar_height = (peak * waveform_height) / 255;
                    if (bar_height < 1) bar_height = 1;
                    if (bar_height > (int)waveform_height) bar_height = waveform_height;
                    
                    int y_start = center_y - (bar_height / 2);
                    int y_end = center_y + (bar_height / 2);
                    if (y_start < 0) y_start = 0;
                    if (y_end >= (int)view_height) y_end = view_height - 1;
                    
                    for (int y = y_start; y <= y_end; y++) {
                        buffer[y * view_width + x] = fg_color;
                    }
                }
            }
        }
    } else if (scroll_delta > 0) {
        // Incremental scroll: shift rows left, draw only new columns on right
        for (int y = 0; y < (int)view_height; y++) {
            lv_color_t *row = &buffer[y * view_width];
            memmove(row, row + scroll_delta, (view_width - scroll_delta) * sizeof(lv_color_t));
        }
        
        // Draw new columns on the right edge
        if (waveform_data && num_samples > 0) {
            int start_x = (int)view_width - scroll_delta;
            if (start_x < 0) start_x = 0;
            
            for (int x = start_x; x < (int)view_width; x++) {
                if (x < (int)num_samples) {
                    draw_bar_at(buffer, x, waveform_data[x], center_y, fg_color, bg_color);
                }
            }
        }
    }
    // If scroll_delta == 0, no update needed (paused or same frame)
    
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
    
    // Progress Bar (bottom 4px) - amber fill bar showing track position
    progress_bar_bg = lv_obj_create(waveform_container);
    lv_obj_set_size(progress_bar_bg, view_width, 4);
    lv_obj_set_align(progress_bar_bg, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_color(progress_bar_bg, lv_color_black(), 0);
    lv_obj_set_style_border_width(progress_bar_bg, 0, 0);
    lv_obj_set_style_pad_all(progress_bar_bg, 0, 0);
    lv_obj_clear_flag(progress_bar_bg, LV_OBJ_FLAG_SCROLLABLE);
    
    progress_bar_cursor = lv_obj_create(progress_bar_bg);
    lv_obj_set_size(progress_bar_cursor, 0, 4); // Starts at 0 width, fills with progress
    lv_obj_set_pos(progress_bar_cursor, 0, 0);
    lv_obj_set_style_bg_color(progress_bar_cursor, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_border_width(progress_bar_cursor, 0, 0);
    lv_obj_set_style_radius(progress_bar_cursor, 0, 0);
    
    // Initially hidden
    lv_obj_add_flag(waveform_container, LV_OBJ_FLAG_HIDDEN);
}

void waveform_view_update(const uint8_t *waveform_data, 
                         size_t num_samples, 
                         float position,
                         size_t wave_index) {
    if (!visible || !waveform_canvas) return;
    
    // Update progress bar fill (position 0.0 to 1.0 = start to end of track)
    if (progress_bar_cursor) {
        int fill_width = (int)(position * view_width);
        if (fill_width < 0) fill_width = 0;
        if (fill_width > (int)view_width) fill_width = view_width;
        lv_obj_set_width(progress_bar_cursor, fill_width);
    }
    
    // Draw waveform with ring buffer scroll optimization
    draw_waveform(waveform_data, num_samples, wave_index);
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

void waveform_view_reset(void) {
    // Reset scroll state for new track
    first_frame = true;
    last_wave_index = 0;
}