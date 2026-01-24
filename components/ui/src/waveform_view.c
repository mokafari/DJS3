/**
 * @file waveform_view.c
 * @brief Waveform display view - Vertical bar graph style with ghosting
 */

#include "waveform_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>
#include <math.h>

static const char *TAG = "waveform_view";

#define WAVEFORM_BARS 480
#define GHOST_FRAMES 3
#define GRID_DOT_SPACING 4

static lv_obj_t *waveform_container = NULL;
static lv_obj_t *waveform_canvas = NULL;
static lv_canvas_t *canvas = NULL;
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
 * @brief Draw vertical bar at position
 */
static void draw_bar(lv_canvas_t *canvas, int x, int height, lv_color_t color) {
    int center_y = waveform_height / 2;
    int top = center_y - height / 2;
    int bottom = center_y + height / 2;
    
    // Draw vertical line (bar)
    for (int y = top; y <= bottom; y++) {
        if (y >= 0 && y < (int)waveform_height) {
            lv_canvas_set_px(canvas, x, y, color, LV_OPA_COVER);
        }
    }
}

/**
 * @brief Draw waveform with ghosting effect
 */
static void draw_waveform(const uint8_t *waveform_data, size_t num_samples, float position) {
    if (!canvas || !visible) return;
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    lv_color_t ghost_color = fg_color;
    
    // Clear canvas
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    // Draw ghost trails (fading)
    for (int i = 0; i < GHOST_FRAMES; i++) {
        int idx = (history_index - i - 1 + GHOST_FRAMES) % GHOST_FRAMES;
        if (waveform_history[idx]) {
            uint8_t alpha = 255 - (i * 85); // Fade out
            lv_opa_t opa = (lv_opa_t)(alpha * LV_OPA_COVER / 255);
            
            // Draw ghost bars
            for (int x = 0; x < WAVEFORM_BARS; x++) {
                uint8_t height_val = waveform_history[idx][x];
                int bar_height = (height_val * waveform_height) / (255 * 2);
                if (bar_height > 0) {
                    lv_color_t ghost = lv_color_mix(ghost_color, lv_color_black(), opa);
                    draw_bar(canvas, x, bar_height, ghost);
                }
            }
        }
    }
    
    // Draw current waveform
    if (waveform_data && num_samples > 0) {
        int samples_per_bar = num_samples / WAVEFORM_BARS;
        if (samples_per_bar < 1) samples_per_bar = 1;
        
        for (int x = 0; x < WAVEFORM_BARS; x++) {
            // Find peak in this bar's sample range
            uint8_t peak = 0;
            int start_idx = x * samples_per_bar;
            int end_idx = (x + 1) * samples_per_bar;
            if (end_idx > (int)num_samples) end_idx = num_samples;
            
            for (int i = start_idx; i < end_idx; i++) {
                if (waveform_data[i] > peak) {
                    peak = waveform_data[i];
                }
            }
            
            // Draw bar
            int bar_height = (peak * waveform_height) / (255 * 2);
            if (bar_height > 0) {
                draw_bar(canvas, x, bar_height, fg_color);
            }
            
            // Store in history
            if (waveform_history[history_index]) {
                waveform_history[history_index][x] = peak;
            }
        }
    }
    
    // Draw grid lines (dotted vertical lines)
    if (beat_positions && num_beats > 0) {
        lv_color_t grid_color = fg_color;
        for (size_t i = 0; i < num_beats; i++) {
            int x = (int)(beat_positions[i] * WAVEFORM_BARS);
            if (x >= 0 && x < WAVEFORM_BARS) {
                // Draw dotted line
                for (int y = 0; y < (int)waveform_height; y += GRID_DOT_SPACING * 2) {
                    lv_canvas_set_px(canvas, x, y, grid_color, LV_OPA_COVER);
                }
            }
        }
    }
    
    // Update history index
    history_index = (history_index + 1) % GHOST_FRAMES;
    
    // Invalidate canvas
    lv_obj_invalidate(waveform_canvas);
}

void waveform_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Waveform view initialized: %ux%u", width, height);
    
    view_width = width;
    view_height = height * 50 / 100; // Zone A: 50% height
    waveform_height = view_height;
    
    // Allocate history buffers
    for (int i = 0; i < GHOST_FRAMES; i++) {
        waveform_history[i] = (uint8_t*)heap_caps_malloc(
            WAVEFORM_BARS * sizeof(uint8_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (waveform_history[i]) {
            memset(waveform_history[i], 0, WAVEFORM_BARS);
        }
    }
    
    // Create container
    waveform_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(waveform_container, view_width, view_height);
    lv_obj_set_pos(waveform_container, 0, height * 20 / 100); // Below metadata zone
    lv_obj_set_style_bg_color(waveform_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(waveform_container, 0, 0);
    lv_obj_set_style_pad_all(waveform_container, 0, 0);
    lv_obj_clear_flag(waveform_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create canvas for waveform
    waveform_canvas = lv_canvas_create(waveform_container);
    canvas = (lv_canvas_t*)waveform_canvas;
    lv_canvas_set_buffer(canvas, 
        heap_caps_malloc(view_width * view_height * sizeof(lv_color_t), MALLOC_CAP_SPIRAM),
        view_width, view_height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(waveform_canvas, view_width, view_height);
    lv_obj_set_pos(waveform_canvas, 0, 0);
    
    // Create playhead line (center vertical line)
    playhead_line = lv_obj_create(waveform_container);
    lv_obj_set_size(playhead_line, 2, view_height);
    lv_obj_set_pos(playhead_line, view_width / 2 - 1, 0);
    lv_obj_set_style_bg_color(playhead_line, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(playhead_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(playhead_line, 0, 0);
    lv_obj_clear_flag(playhead_line, LV_OBJ_FLAG_CLICKABLE);
    
    // Create cursor line (touch feedback)
    cursor_line = lv_obj_create(waveform_container);
    lv_obj_set_size(cursor_line, 2, view_height);
    lv_obj_set_pos(cursor_line, 0, 0);
    lv_obj_set_style_bg_color(cursor_line, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(cursor_line, LV_OPA_50, 0);
    lv_obj_set_style_border_width(cursor_line, 0, 0);
    lv_obj_add_flag(cursor_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cursor_line, LV_OBJ_FLAG_CLICKABLE);
    
    // Initially hidden
    lv_obj_add_flag(waveform_container, LV_OBJ_FLAG_HIDDEN);
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

void waveform_view_update(const uint8_t *waveform_data, 
                         size_t num_samples, 
                         float position) {
    if (!visible || !canvas) return;
    
    // Update playhead position
    if (playhead_line) {
        int playhead_x = (int)(position * view_width);
        lv_obj_set_x(playhead_line, playhead_x - 1);
    }
    
    // Draw waveform
    draw_waveform(waveform_data, num_samples, position);
}

void waveform_view_update_grid(const float *beat_positions_in, size_t num_beats_in) {
    // Free old grid data
    if (beat_positions) {
        free(beat_positions);
        beat_positions = NULL;
    }
    
    num_beats = num_beats_in;
    if (num_beats > 0 && beat_positions_in) {
        beat_positions = (float*)malloc(num_beats * sizeof(float));
        if (beat_positions) {
            memcpy(beat_positions, beat_positions_in, num_beats * sizeof(float));
        }
    }
}

void waveform_view_show_cursor(float position, bool visible_cursor) {
    if (!cursor_line) return;
    
    if (visible_cursor) {
        int cursor_x = (int)(position * view_width);
        lv_obj_set_x(cursor_line, cursor_x - 1);
        lv_obj_clear_flag(cursor_line, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cursor_line, LV_OBJ_FLAG_HIDDEN);
    }
}
