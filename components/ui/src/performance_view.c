/**
 * @file performance_view.c
 * @brief Performance mode view - Simplified UI for live DJ use
 * 
 * Maximizes waveform display area with only essential controls:
 * - Play/pause button
 * - Cue button
 * - Loop controls (in/out, toggle)
 * 
 * Optimized for dark club environments with high contrast display.
 * Large touch targets for reliability during live performance.
 */

#include "performance_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "audio_player.h"
#include "cue_markers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>
#include <math.h>

static const char *TAG = "performance_view";

// ============================================================================
// CONFIGURATION
// ============================================================================

#define PERF_WAVEFORM_BARS      480     // Full width waveform bars
#define PERF_CONTROL_HEIGHT     60      // Bottom control strip height (large touch targets)
#define PERF_PLAYHEAD_WIDTH     3       // Bright playhead line
#define PERF_BUTTON_SIZE        50      // Large buttons for reliability
#define PERF_BUTTON_SPACING     20      // Space between buttons
#define PERF_LOOP_INDICATOR_H   4       // Loop region indicator height

// Frame throttling
#define PERF_MIN_FRAME_US       20000   // Max ~50 FPS

// ============================================================================
// STATE
// ============================================================================

static lv_obj_t *perf_container = NULL;         // Main container (full screen)
static lv_obj_t *waveform_canvas = NULL;        // Large waveform canvas
static lv_obj_t *playhead_line = NULL;          // Center playhead
static lv_obj_t *control_strip = NULL;          // Bottom control bar
static lv_obj_t *loop_indicator = NULL;         // Loop region highlight

// Control buttons
static lv_obj_t *btn_play = NULL;
static lv_obj_t *btn_cue = NULL;
static lv_obj_t *btn_loop_in = NULL;
static lv_obj_t *btn_loop_out = NULL;
static lv_obj_t *btn_loop_toggle = NULL;
static lv_obj_t *btn_exit = NULL;               // Exit to normal view

// Button labels
static lv_obj_t *lbl_play = NULL;
static lv_obj_t *lbl_cue = NULL;
static lv_obj_t *lbl_loop_in = NULL;
static lv_obj_t *lbl_loop_out = NULL;
static lv_obj_t *lbl_loop = NULL;
static lv_obj_t *lbl_exit = NULL;

// Time display (minimal)
static lv_obj_t *time_label = NULL;

// Dimensions
static uint32_t view_width = 0;
static uint32_t view_height = 0;
static uint32_t waveform_height = 0;

// State
static bool visible = false;
static bool is_playing = false;
static bool is_looping = false;
static float loop_in_pos = -1.0f;               // -1 = not set
static float loop_out_pos = -1.0f;

// Frame throttling
static uint64_t last_draw_time = 0;

// Waveform display cache (stable past data)
static uint8_t display_cache[PERF_WAVEFORM_BARS];
static size_t last_wave_index = 0;
static bool first_frame = true;
static bool cache_valid = false;

// Exit callback
static performance_view_exit_cb_t exit_callback = NULL;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void draw_waveform(const uint8_t *data, size_t samples, size_t wave_index);
static void update_button_states(void);
static void update_loop_indicator(void);

// ============================================================================
// EVENT HANDLERS
// ============================================================================

/**
 * @brief Play/Pause button handler
 */
static void on_play_clicked(lv_event_t *e) {
    (void)e;
    audio_player_state_t state = audio_player_get_state();
    
    if (state == AUDIO_PLAYER_STATE_PLAYING) {
        audio_player_pause();
        is_playing = false;
    } else {
        audio_player_play();
        is_playing = true;
    }
    
    update_button_states();
    ESP_LOGI(TAG, "Play/Pause: %s", is_playing ? "PLAY" : "PAUSE");
}

/**
 * @brief Cue button handler (press and hold = preview, release = return)
 */
static void on_cue_pressed(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        // Store current position if playing, then jump to cue
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
            audio_player_pause();
        }
        // Jump to cue point (position 0 if no cue set)
        audio_player_seek(0);
        audio_player_play();
        ESP_LOGI(TAG, "CUE: Preview");
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        // Return to cue point
        audio_player_pause();
        audio_player_seek(0);
        is_playing = false;
        update_button_states();
        ESP_LOGI(TAG, "CUE: Return");
    }
}

/**
 * @brief Loop IN button handler
 */
static void on_loop_in_clicked(lv_event_t *e) {
    (void)e;
    float pos = (float)audio_player_get_position() / 
                (float)audio_player_get_duration();
    float pos_sec = audio_player_get_precise_position();
    
    loop_in_pos = pos;
    
    // Update cue markers system
    if (loop_out_pos >= 0) {
        cue_markers_set_loop(loop_in_pos, loop_out_pos, 
                             pos_sec, loop_out_pos * audio_player_get_duration());
    }
    
    update_loop_indicator();
    update_button_states();
    ESP_LOGI(TAG, "Loop IN set at %.1f%%", loop_in_pos * 100);
}

/**
 * @brief Loop OUT button handler
 */
static void on_loop_out_clicked(lv_event_t *e) {
    (void)e;
    float pos = (float)audio_player_get_position() / 
                (float)audio_player_get_duration();
    float pos_sec = audio_player_get_precise_position();
    
    loop_out_pos = pos;
    
    // Update cue markers system
    if (loop_in_pos >= 0) {
        cue_markers_set_loop(loop_in_pos, loop_out_pos,
                             loop_in_pos * audio_player_get_duration(), pos_sec);
        // Auto-enable loop when both points are set
        is_looping = true;
        cue_markers_set_loop_enabled(true);
    }
    
    update_loop_indicator();
    update_button_states();
    ESP_LOGI(TAG, "Loop OUT set at %.1f%%", loop_out_pos * 100);
}

/**
 * @brief Loop toggle button handler
 */
static void on_loop_toggle_clicked(lv_event_t *e) {
    (void)e;
    
    if (loop_in_pos < 0 || loop_out_pos < 0) {
        ESP_LOGW(TAG, "Loop not set - need IN and OUT points");
        return;
    }
    
    is_looping = !is_looping;
    cue_markers_set_loop_enabled(is_looping);
    
    update_button_states();
    ESP_LOGI(TAG, "Loop %s", is_looping ? "ENABLED" : "DISABLED");
}

/**
 * @brief Exit button handler - return to normal view
 */
static void on_exit_clicked(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "Exit performance mode");
    
    if (exit_callback) {
        exit_callback();
    }
}

// ============================================================================
// BUTTON STYLING
// ============================================================================

/**
 * @brief Create a high-contrast performance button
 */
static lv_obj_t* create_perf_button(lv_obj_t *parent, const char *text, 
                                     lv_obj_t **label_out, bool is_square) {
    lv_obj_t *btn = lv_btn_create(parent);
    
    // Large touch target
    if (is_square) {
        lv_obj_set_size(btn, PERF_BUTTON_SIZE, PERF_BUTTON_SIZE);
    } else {
        lv_obj_set_size(btn, PERF_BUTTON_SIZE * 1.5, PERF_BUTTON_SIZE);
    }
    
    // High contrast styling - bright border, dark fill
    lv_color_t fg = hud_theme_get_foreground_color();
    lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, fg, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 4, LV_STATE_DEFAULT);
    
    // Pressed state - invert colors
    lv_obj_set_style_bg_color(btn, fg, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, fg, LV_STATE_PRESSED);
    
    // Checked state (for toggle buttons like loop)
    lv_obj_set_style_bg_color(btn, fg, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btn, fg, LV_STATE_CHECKED);
    
    // Label
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, fg, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lbl, lv_color_black(), LV_STATE_CHECKED);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl);
    
    if (label_out) {
        *label_out = lbl;
    }
    
    return btn;
}

/**
 * @brief Update button visual states based on current state
 */
static void update_button_states(void) {
    lv_color_t fg = hud_theme_get_foreground_color();
    lv_color_t active_color = lv_color_make(0, 255, 100);  // Bright green for active
    
    // Play button - show state
    if (lbl_play) {
        lv_label_set_text(lbl_play, is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    
    // Loop button - show enabled state
    if (btn_loop_toggle) {
        if (is_looping) {
            lv_obj_add_state(btn_loop_toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(btn_loop_toggle, LV_STATE_CHECKED);
        }
    }
    
    // Loop in/out buttons - highlight when set
    if (btn_loop_in && loop_in_pos >= 0) {
        lv_obj_set_style_border_color(btn_loop_in, active_color, LV_STATE_DEFAULT);
    } else if (btn_loop_in) {
        lv_obj_set_style_border_color(btn_loop_in, fg, LV_STATE_DEFAULT);
    }
    
    if (btn_loop_out && loop_out_pos >= 0) {
        lv_obj_set_style_border_color(btn_loop_out, active_color, LV_STATE_DEFAULT);
    } else if (btn_loop_out) {
        lv_obj_set_style_border_color(btn_loop_out, fg, LV_STATE_DEFAULT);
    }
}

/**
 * @brief Update loop region indicator on waveform
 */
static void update_loop_indicator(void) {
    if (!loop_indicator || !visible) return;
    
    if (loop_in_pos >= 0 && loop_out_pos >= 0 && loop_out_pos > loop_in_pos) {
        int start_x = (int)(loop_in_pos * view_width);
        int end_x = (int)(loop_out_pos * view_width);
        int width = end_x - start_x;
        
        if (width > 2) {
            lv_obj_set_pos(loop_indicator, start_x, 0);
            lv_obj_set_width(loop_indicator, width);
            lv_obj_clear_flag(loop_indicator, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(loop_indicator, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================================================
// WAVEFORM RENDERING
// ============================================================================

/**
 * @brief Update display cache with stable past data
 */
static void update_cache(const uint8_t *source, size_t len, size_t new_index) {
    if (!source || len == 0) return;
    
    int delta = (int)new_index - (int)last_wave_index;
    
    if (!cache_valid || delta < 0 || delta > PERF_WAVEFORM_BARS / 2) {
        // Full refresh on seek or first frame
        size_t copy_len = (len < PERF_WAVEFORM_BARS) ? len : PERF_WAVEFORM_BARS;
        memcpy(display_cache, source, copy_len);
        if (copy_len < PERF_WAVEFORM_BARS) {
            memset(display_cache + copy_len, 0, PERF_WAVEFORM_BARS - copy_len);
        }
        cache_valid = true;
    } else if (delta > 0) {
        // Incremental update - shift left, add new on right
        memmove(display_cache, display_cache + delta, PERF_WAVEFORM_BARS - delta);
        
        int src_start = PERF_WAVEFORM_BARS / 2;
        int cache_start = PERF_WAVEFORM_BARS - delta;
        for (int i = 0; i < delta && cache_start + i < PERF_WAVEFORM_BARS; i++) {
            int src_idx = src_start + (PERF_WAVEFORM_BARS / 2 - delta) + i;
            if (src_idx >= 0 && src_idx < (int)len) {
                display_cache[cache_start + i] = source[src_idx];
            } else {
                display_cache[cache_start + i] = 0;
            }
        }
    }
    
    last_wave_index = new_index;
}

/**
 * @brief Draw waveform with maximum visibility
 * 
 * High contrast rendering optimized for dark environments.
 * Full width bars for maximum visual impact.
 */
static void draw_waveform(const uint8_t *data, size_t samples, size_t wave_index) {
    if (!waveform_canvas || !visible) return;
    
    // Frame throttling
    uint64_t now = esp_timer_get_time();
    if (!first_frame && (now - last_draw_time) < PERF_MIN_FRAME_US) {
        return;
    }
    last_draw_time = now;
    
    lv_img_dsc_t *img = lv_canvas_get_img(waveform_canvas);
    if (!img) return;
    lv_color_t *buffer = (lv_color_t *)img->data;
    if (!buffer) return;
    
    // Update stable cache
    update_cache(data, samples, wave_index);
    
    lv_color_t fg = hud_theme_get_foreground_color();
    lv_color_t bg = lv_color_black();
    int center_y = waveform_height / 2;
    int max_bar_height = waveform_height - 10;  // Leave small margin
    
    // Calculate scroll
    bool need_full = first_frame;
    int scroll_delta = (int)wave_index - (int)last_wave_index;
    if (!first_frame && (scroll_delta < 0 || scroll_delta > 50)) {
        need_full = true;
    }
    first_frame = false;
    
    // Clear and draw
    size_t buf_size = view_width * waveform_height;
    memset(buffer, 0, buf_size * sizeof(lv_color_t));
    
    // Draw waveform bars at full resolution
    for (int x = 0; x < (int)view_width && x < PERF_WAVEFORM_BARS; x++) {
        uint8_t peak = display_cache[x];
        if (peak < 3) continue;  // Skip very quiet parts
        
        int bar_height = (peak * max_bar_height) / 255;
        if (bar_height < 2) bar_height = 2;
        
        int y_start = center_y - (bar_height / 2);
        int y_end = center_y + (bar_height / 2);
        if (y_start < 0) y_start = 0;
        if (y_end >= (int)waveform_height) y_end = waveform_height - 1;
        
        for (int y = y_start; y <= y_end; y++) {
            buffer[y * view_width + x] = fg;
        }
    }
    
    // Draw center line (subtle reference)
    lv_color_t dim = lv_color_mix(fg, bg, 64);  // 25% opacity
    for (int x = 0; x < (int)view_width; x += 4) {
        buffer[center_y * view_width + x] = dim;
    }
    
    lv_obj_invalidate(waveform_canvas);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void performance_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Performance view init: %ux%u", width, height);
    
    view_width = width;
    view_height = height;
    waveform_height = height - PERF_CONTROL_HEIGHT;
    
    // Main container (full screen, black background)
    perf_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(perf_container, width, height);
    lv_obj_set_pos(perf_container, 0, 0);
    lv_obj_set_style_bg_color(perf_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(perf_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(perf_container, 0, 0);
    lv_obj_set_style_pad_all(perf_container, 0, 0);
    lv_obj_clear_flag(perf_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // ========================================================================
    // WAVEFORM CANVAS (maximized)
    // ========================================================================
    waveform_canvas = lv_canvas_create(perf_container);
    
    size_t buf_size = view_width * waveform_height * sizeof(lv_color_t);
    ESP_LOGI(TAG, "Allocating waveform buffer: %zu bytes", buf_size);
    
    // Try internal RAM first for speed
    void *canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!canvas_buf) {
        ESP_LOGW(TAG, "Internal RAM failed, using PSRAM");
        canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    
    if (canvas_buf) {
        lv_canvas_set_buffer(waveform_canvas, canvas_buf, view_width, 
                             waveform_height, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_size(waveform_canvas, view_width, waveform_height);
        lv_obj_set_pos(waveform_canvas, 0, 0);
        lv_canvas_fill_bg(waveform_canvas, lv_color_black(), LV_OPA_COVER);
    } else {
        ESP_LOGE(TAG, "Failed to allocate waveform buffer!");
    }
    
    // ========================================================================
    // LOOP INDICATOR (top of waveform)
    // ========================================================================
    loop_indicator = lv_obj_create(perf_container);
    lv_obj_set_size(loop_indicator, 0, PERF_LOOP_INDICATOR_H);
    lv_obj_set_style_bg_color(loop_indicator, lv_color_make(0, 255, 100), 0);
    lv_obj_set_style_bg_opa(loop_indicator, LV_OPA_70, 0);
    lv_obj_set_style_border_width(loop_indicator, 0, 0);
    lv_obj_add_flag(loop_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(loop_indicator, LV_OBJ_FLAG_CLICKABLE);
    
    // ========================================================================
    // PLAYHEAD (center, full height, bright)
    // ========================================================================
    playhead_line = lv_obj_create(perf_container);
    lv_obj_set_size(playhead_line, PERF_PLAYHEAD_WIDTH, waveform_height);
    lv_obj_set_pos(playhead_line, (view_width / 2) - (PERF_PLAYHEAD_WIDTH / 2), 0);
    lv_obj_set_style_bg_color(playhead_line, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(playhead_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(playhead_line, 0, 0);
    lv_obj_clear_flag(playhead_line, LV_OBJ_FLAG_CLICKABLE);
    
    // ========================================================================
    // TIME DISPLAY (top right, minimal)
    // ========================================================================
    time_label = lv_label_create(perf_container);
    lv_obj_set_style_text_color(time_label, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(time_label, "-00:00");
    lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    // ========================================================================
    // CONTROL STRIP (bottom)
    // ========================================================================
    control_strip = lv_obj_create(perf_container);
    lv_obj_set_size(control_strip, view_width, PERF_CONTROL_HEIGHT);
    lv_obj_set_pos(control_strip, 0, waveform_height);
    lv_obj_set_style_bg_color(control_strip, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_bg_opa(control_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(control_strip, 0, 0);
    lv_obj_set_style_pad_all(control_strip, 5, 0);
    lv_obj_clear_flag(control_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(control_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_strip, LV_FLEX_ALIGN_SPACE_EVENLY, 
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Exit button (left)
    btn_exit = create_perf_button(control_strip, LV_SYMBOL_CLOSE, &lbl_exit, true);
    lv_obj_add_event_cb(btn_exit, on_exit_clicked, LV_EVENT_CLICKED, NULL);
    
    // Play/Pause button
    btn_play = create_perf_button(control_strip, LV_SYMBOL_PLAY, &lbl_play, false);
    lv_obj_add_event_cb(btn_play, on_play_clicked, LV_EVENT_CLICKED, NULL);
    
    // Cue button (press and hold)
    btn_cue = create_perf_button(control_strip, "CUE", &lbl_cue, false);
    lv_obj_add_event_cb(btn_cue, on_cue_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btn_cue, on_cue_pressed, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(btn_cue, on_cue_pressed, LV_EVENT_PRESS_LOST, NULL);
    
    // Loop IN button
    btn_loop_in = create_perf_button(control_strip, "IN", &lbl_loop_in, true);
    lv_obj_add_event_cb(btn_loop_in, on_loop_in_clicked, LV_EVENT_CLICKED, NULL);
    
    // Loop OUT button
    btn_loop_out = create_perf_button(control_strip, "OUT", &lbl_loop_out, true);
    lv_obj_add_event_cb(btn_loop_out, on_loop_out_clicked, LV_EVENT_CLICKED, NULL);
    
    // Loop toggle button (checkable)
    btn_loop_toggle = create_perf_button(control_strip, LV_SYMBOL_LOOP, &lbl_loop, true);
    lv_obj_add_flag(btn_loop_toggle, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn_loop_toggle, on_loop_toggle_clicked, LV_EVENT_CLICKED, NULL);
    
    // Initially hidden
    lv_obj_add_flag(perf_container, LV_OBJ_FLAG_HIDDEN);
    
    // Initialize state
    visible = false;
    first_frame = true;
    cache_valid = false;
    
    ESP_LOGI(TAG, "Performance view initialized");
}

void performance_view_deinit(void) {
    if (waveform_canvas) {
        lv_img_dsc_t *img = lv_canvas_get_img(waveform_canvas);
        if (img && img->data) {
            heap_caps_free((void*)img->data);
        }
    }
    
    if (perf_container) {
        lv_obj_del(perf_container);
        perf_container = NULL;
    }
    
    visible = false;
    ESP_LOGI(TAG, "Performance view deinitialized");
}

void performance_view_show(void) {
    if (!perf_container) return;
    
    visible = true;
    first_frame = true;
    cache_valid = false;
    
    // Sync state with audio player
    is_playing = (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING);
    
    // Check for existing loop
    const cue_loop_region_t *loop = cue_markers_get_loop();
    if (loop && loop->active) {
        loop_in_pos = loop->start_position;
        loop_out_pos = loop->end_position;
        is_looping = loop->enabled;
    }
    
    update_button_states();
    update_loop_indicator();
    
    lv_obj_clear_flag(perf_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(perf_container);
    
    ESP_LOGI(TAG, "Performance view shown");
}

void performance_view_hide(void) {
    if (!perf_container) return;
    
    visible = false;
    lv_obj_add_flag(perf_container, LV_OBJ_FLAG_HIDDEN);
    
    ESP_LOGI(TAG, "Performance view hidden");
}

bool performance_view_is_visible(void) {
    return visible;
}

void performance_view_update(const uint8_t *waveform_data, size_t num_samples,
                              float position, float precise_time, size_t wave_index) {
    if (!visible || !perf_container) return;
    
    // Update waveform
    draw_waveform(waveform_data, num_samples, wave_index);
    
    // Update time display (remaining time)
    if (time_label) {
        uint32_t duration = audio_player_get_duration();
        uint32_t pos = audio_player_get_position();
        int32_t remaining = (int32_t)duration - (int32_t)pos;
        if (remaining < 0) remaining = 0;
        
        int mins = remaining / 60;
        int secs = remaining % 60;
        
        char buf[16];
        snprintf(buf, sizeof(buf), "-%02d:%02d", mins, secs);
        lv_label_set_text(time_label, buf);
    }
    
    // Check loop boundary (auto-loop back if enabled)
    if (is_looping && loop_in_pos >= 0 && loop_out_pos > loop_in_pos) {
        if (position >= loop_out_pos) {
            uint32_t loop_in_sec = (uint32_t)(loop_in_pos * audio_player_get_duration());
            audio_player_seek(loop_in_sec);
            ESP_LOGI(TAG, "Loop: jumped back to IN point");
        }
    }
    
    // Sync play state
    bool now_playing = (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING);
    if (now_playing != is_playing) {
        is_playing = now_playing;
        update_button_states();
    }
}

void performance_view_reset(void) {
    first_frame = true;
    cache_valid = false;
    last_wave_index = 0;
    memset(display_cache, 0, sizeof(display_cache));
    
    // Clear loop state
    loop_in_pos = -1.0f;
    loop_out_pos = -1.0f;
    is_looping = false;
    
    if (loop_indicator) {
        lv_obj_add_flag(loop_indicator, LV_OBJ_FLAG_HIDDEN);
    }
    
    update_button_states();
    
    ESP_LOGI(TAG, "Performance view reset");
}

void performance_view_set_exit_callback(performance_view_exit_cb_t callback) {
    exit_callback = callback;
}
