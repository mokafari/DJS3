/**
 * @file waveform_view.c
 * @brief Waveform display view - Scrolling, optimized buffer access
 */

#include "waveform_view.h"
#include "hud_theme.h"
#include "lvgl_driver.h"
#include "audio_player.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>
#include <math.h>

static const char *TAG = "waveform_view";

#define WAVEFORM_BARS 480
#define GRID_DOT_SPACING 4

// ============================================================================
// PERFORMANCE PROFILING
// ============================================================================
#define PERF_ENABLED 1              // Set to 0 to disable profiling
#define PERF_LOG_INTERVAL 100       // Log stats every N frames
#define PERF_ROLLING_WINDOW 30      // Rolling average window size

#if PERF_ENABLED
static uint64_t perf_frame_times[PERF_ROLLING_WINDOW];
static uint64_t perf_cache_times[PERF_ROLLING_WINDOW];
static uint64_t perf_draw_times[PERF_ROLLING_WINDOW];
static uint64_t perf_invalidate_times[PERF_ROLLING_WINDOW];
static int perf_index = 0;
static int perf_frame_count = 0;
static int perf_full_redraws = 0;
static int perf_incremental_draws = 0;
static int perf_skip_draws = 0;

static inline uint64_t perf_get_time_us(void) {
    return esp_timer_get_time();
}

static void perf_log_stats(void) {
    if (perf_frame_count == 0) return;
    
    // Calculate averages
    uint64_t total_frame = 0, total_cache = 0, total_draw = 0, total_invalidate = 0;
    int samples = (perf_frame_count < PERF_ROLLING_WINDOW) ? perf_frame_count : PERF_ROLLING_WINDOW;
    
    for (int i = 0; i < samples; i++) {
        total_frame += perf_frame_times[i];
        total_cache += perf_cache_times[i];
        total_draw += perf_draw_times[i];
        total_invalidate += perf_invalidate_times[i];
    }
    
    uint64_t avg_frame = total_frame / samples;
    uint64_t avg_cache = total_cache / samples;
    uint64_t avg_draw = total_draw / samples;
    uint64_t avg_invalidate = total_invalidate / samples;
    
    float fps = (avg_frame > 0) ? 1000000.0f / avg_frame : 0;
    
    ESP_LOGI(TAG, "PERF: %.1f FPS | frame=%lluus cache=%lluus draw=%lluus inv=%lluus | full=%d inc=%d skip=%d",
             fps, 
             (unsigned long long)avg_frame,
             (unsigned long long)avg_cache, 
             (unsigned long long)avg_draw,
             (unsigned long long)avg_invalidate,
             perf_full_redraws, perf_incremental_draws, perf_skip_draws);
    
    // Reset counters
    perf_full_redraws = 0;
    perf_incremental_draws = 0;
    perf_skip_draws = 0;
}
#endif

// Compile-time default resolution (can be overridden at runtime)
// Trade-off: Lower values = more detail, Higher = better performance
// Recommended: 2-4 for 240MHz ESP32-S3 with PSRAM canvas
#ifndef WAVEFORM_DEFAULT_RESOLUTION
#define WAVEFORM_DEFAULT_RESOLUTION 4  // 1=full, 2=half, 4=quarter (DEFAULT), 8=eighth
#endif

static lv_obj_t *waveform_container = NULL;
static lv_obj_t *waveform_canvas = NULL;
static lv_obj_t *playhead_line = NULL;
// static lv_obj_t *cursor_line = NULL;    // Reserved for touch feedback
// static lv_obj_t *grid_container = NULL; // Reserved for beat grid overlay

static uint32_t view_width = 0;
static uint32_t view_height = 0;
static uint32_t waveform_height = 0;
static bool visible = false;

// Resolution control (1=480 bars, 2=240 bars, 4=120 bars, 8=60 bars)
static int resolution_divider = WAVEFORM_DEFAULT_RESOLUTION;

// Ring buffer scroll optimization
static size_t last_wave_index = 0;
static bool first_frame = true;
#define SCROLL_DELTA_THRESHOLD 30  // Full redraw if scroll > 30 pixels

// Nudge animation state
static int nudge_offset_px = 0;           // Current pixel offset (decaying)
static int64_t nudge_start_time = 0;      // When nudge started (microseconds)
static const int NUDGE_AMOUNT_PX = 20;    // Max jerk distance in pixels
static const int NUDGE_DURATION_MS = 100; // Animation duration

// Forward declaration for nudge offset calculation
static int calculate_nudge_offset(void);

// Frame throttling - minimum pixels to scroll before update
#define MIN_SCROLL_FOR_UPDATE 2    // Don't redraw for tiny movements
static uint64_t last_draw_time_us = 0;
#define MIN_FRAME_INTERVAL_US 25000  // Max ~40 FPS for waveform (save CPU)

// Forward declaration
static int calculate_nudge_offset(void);

// Stable display cache - prevents past data from changing
static uint8_t display_cache[WAVEFORM_BARS];
static size_t display_cache_center_index = 0;
static bool display_cache_valid = false;

// Grid lines
static float *beat_positions = NULL;
static size_t num_beats = 0;

/**
 * @brief Update display cache with new waveform data
 * 
 * Only updates RIGHT side (future) of cache. LEFT side (past) stays stable.
 * This prevents the waveform from visually changing behind the playhead.
 */
static void update_display_cache(const uint8_t *source, size_t source_len, size_t new_center_index) {
    if (!source || source_len == 0) return;
    
    int delta = (int)new_center_index - (int)display_cache_center_index;
    
    if (!display_cache_valid || delta < 0 || delta > WAVEFORM_BARS / 2) {
        // Full cache refresh: seek, scrub backwards, or first frame
        size_t copy_len = (source_len < WAVEFORM_BARS) ? source_len : WAVEFORM_BARS;
        memcpy(display_cache, source, copy_len);
        if (copy_len < WAVEFORM_BARS) {
            memset(display_cache + copy_len, 0, WAVEFORM_BARS - copy_len);
        }
        display_cache_valid = true;
    } else if (delta > 0) {
        // Incremental update: shift left, copy new data on right
        memmove(display_cache, display_cache + delta, WAVEFORM_BARS - delta);
        
        // Copy new right-side data from source (right half = future audio)
        int src_start = WAVEFORM_BARS / 2;  // Start from center (playhead position in source)
        int cache_start = WAVEFORM_BARS - delta;
        for (int i = 0; i < delta && cache_start + i < WAVEFORM_BARS; i++) {
            int src_idx = src_start + (WAVEFORM_BARS / 2 - delta) + i;
            if (src_idx >= 0 && src_idx < (int)source_len) {
                display_cache[cache_start + i] = source[src_idx];
            } else {
                display_cache[cache_start + i] = 0;
            }
        }
    }
    // delta == 0: no change needed
    
    display_cache_center_index = new_center_index;
}

/**
 * @brief Get binned peak value (MAX of samples in bin for transient visibility)
 */
static inline uint8_t get_binned_peak(const uint8_t *data, int start_idx, int bin_size, int max_idx) {
    uint8_t max_peak = 0;
    for (int i = 0; i < bin_size && (start_idx + i) < max_idx; i++) {
        if (data[start_idx + i] > max_peak) {
            max_peak = data[start_idx + i];
        }
    }
    return max_peak;
}

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
 * @brief Draw waveform with ring buffer scroll optimization and variable resolution
 * 
 * Uses incremental scrolling: shifts existing pixels left and only draws
 * new columns. Falls back to full redraw on seek/scrub (backwards or large jump).
 * Resolution divider reduces bars drawn for better performance.
 */
static void draw_waveform(const uint8_t *waveform_data, size_t num_samples, size_t wave_index, float precise_time) {
    if (!waveform_canvas || !visible) return;
    
    // Frame throttling: check if we should skip this frame
    uint64_t current_time = esp_timer_get_time();
    int preliminary_delta = (int)wave_index - (int)last_wave_index;
    
    // Check if nudge animation is active (don't skip frames during animation)
    bool nudge_active = (nudge_offset_px > 0);
    
    // Skip frame if: not first frame, small movement, not animating, and not enough time passed
    if (!first_frame && !nudge_active && preliminary_delta >= 0 && preliminary_delta < MIN_SCROLL_FOR_UPDATE) {
        if ((current_time - last_draw_time_us) < MIN_FRAME_INTERVAL_US) {
#if PERF_ENABLED
            perf_skip_draws++;
#endif
            return;  // Skip this frame
        }
    }
    
    // Force full redraw if nudge is animating (need to update all bar positions)
    bool force_redraw_for_nudge = nudge_active;
    
#if PERF_ENABLED
    uint64_t t_start = perf_get_time_us();
    uint64_t t_cache_start, t_cache_end;
    uint64_t t_draw_start, t_draw_end;
#endif
    
    lv_img_dsc_t *canvas_img = lv_canvas_get_img(waveform_canvas);
    lv_color_t *buffer = (lv_color_t *)canvas_img->data;
    if (!buffer) return;
    
#if PERF_ENABLED
    t_cache_start = perf_get_time_us();
#endif
    // Update display cache first (stabilizes past data)
    update_display_cache(waveform_data, num_samples, wave_index);
#if PERF_ENABLED
    t_cache_end = perf_get_time_us();
#endif
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    lv_color_t bg_color = lv_color_black();
    int center_y = view_height / 2;
    
    // Calculate nudge animation offset (jerks waveform left)
    int nudge_x_offset = calculate_nudge_offset();
    
    // Calculate effective bars based on resolution
    int effective_bars = WAVEFORM_BARS / resolution_divider;
    int bar_width = resolution_divider;  // Each bar spans this many pixels
    
    // Calculate scroll delta since last frame (in display cache units)
    int scroll_delta = 0;
    bool need_full_redraw = first_frame || force_redraw_for_nudge;
    
    if (!first_frame) {
        scroll_delta = (int)wave_index - (int)last_wave_index;
        
        // Full redraw if: backwards scroll (scrubbing) OR large jump (seek)
        if (scroll_delta < 0 || scroll_delta > SCROLL_DELTA_THRESHOLD) {
            need_full_redraw = true;
        }
    }
    
    first_frame = false;
    last_wave_index = wave_index;
    
    // Scale scroll delta for resolution
    int pixel_scroll = scroll_delta * bar_width / resolution_divider;
    if (pixel_scroll < 0) pixel_scroll = 0;
    
#if PERF_ENABLED
    t_draw_start = perf_get_time_us();
#endif
    
    if (need_full_redraw) {
#if PERF_ENABLED
        perf_full_redraws++;
#endif
        // Full redraw: clear buffer with memset (fastest)
        size_t buffer_size_bytes = view_width * view_height * sizeof(lv_color_t);
        memset(buffer, 0, buffer_size_bytes);
        
        // Draw bar by bar but access memory in row-friendly pattern
        for (int bar = 0; bar < effective_bars; bar++) {
            int data_start = bar * resolution_divider;
            uint8_t peak = get_binned_peak(display_cache, data_start, resolution_divider, WAVEFORM_BARS);
            
            if (peak > 0) {
                int bar_height = (peak * waveform_height) / 255;
                if (bar_height < 1) bar_height = 1;
                if (bar_height > (int)waveform_height) bar_height = waveform_height;
                
                int y_start = center_y - (bar_height / 2);
                int y_end = center_y + (bar_height / 2);
                if (y_start < 0) y_start = 0;
                if (y_end >= (int)view_height) y_end = view_height - 1;
                
                // Apply nudge offset (shifts waveform left)
                int x_start = bar * bar_width - nudge_x_offset;
                int x_end = x_start + bar_width;
                if (x_start < 0) x_start = 0;
                if (x_end > (int)view_width) x_end = view_width;
                if (x_start >= x_end) continue;  // Bar completely off-screen
                
                // Draw bar row by row for better cache behavior
                for (int y = y_start; y <= y_end; y++) {
                    lv_color_t *row = &buffer[y * view_width];
                    for (int x = x_start; x < x_end; x++) {
                        row[x] = fg_color;
                    }
                }
            }
        }
    } else if (scroll_delta > 0 && pixel_scroll > 0) {
#if PERF_ENABLED
        perf_incremental_draws++;
#endif
        // Incremental scroll: shift buffer left and draw new bars on right
        int shift_amount = pixel_scroll;
        if (shift_amount > (int)view_width) shift_amount = view_width;
        
        // Optimized row shift: process in cache-friendly chunks
        // Each row is view_width * sizeof(lv_color_t) bytes
        size_t row_bytes = view_width * sizeof(lv_color_t);
        size_t shift_bytes = shift_amount * sizeof(lv_color_t);
        size_t copy_bytes = row_bytes - shift_bytes;
        
        for (int y = 0; y < (int)view_height; y++) {
            uint8_t *row = (uint8_t *)&buffer[y * view_width];
            memmove(row, row + shift_bytes, copy_bytes);
        }
        
        // Calculate which bars need drawing (rightmost bars after shift)
        int bars_to_draw = (scroll_delta + resolution_divider - 1) / resolution_divider + 1;
        int start_bar = effective_bars - bars_to_draw;
        if (start_bar < 0) start_bar = 0;
        
        // Pre-calculate x range for all new bars
        int clear_x_start = start_bar * bar_width;
        int clear_x_end = (int)view_width;
        
        // Bulk clear the new area (row by row for cache efficiency)
        for (int y = 0; y < (int)view_height; y++) {
            lv_color_t *row = &buffer[y * view_width + clear_x_start];
            for (int x = 0; x < clear_x_end - clear_x_start; x++) {
                row[x] = bg_color;
            }
        }
        
        // Draw new bars
        for (int bar = start_bar; bar < effective_bars; bar++) {
            int data_start = bar * resolution_divider;
            uint8_t peak = get_binned_peak(display_cache, data_start, resolution_divider, WAVEFORM_BARS);
            
            if (peak > 0) {
                int bar_height = (peak * waveform_height) / 255;
                if (bar_height < 1) bar_height = 1;
                if (bar_height > (int)waveform_height) bar_height = waveform_height;
                
                int y_start = center_y - (bar_height / 2);
                int y_end = center_y + (bar_height / 2);
                if (y_start < 0) y_start = 0;
                if (y_end >= (int)view_height) y_end = view_height - 1;
                
                // Apply nudge offset (shifts waveform left)
                int x_start = bar * bar_width - nudge_x_offset;
                int x_end = x_start + bar_width;
                if (x_start < 0) x_start = 0;
                if (x_end > (int)view_width) x_end = view_width;
                if (x_start >= x_end) continue;  // Bar completely off-screen
                
                // Draw bar row by row (cache-friendly)
                for (int y = y_start; y <= y_end; y++) {
                    lv_color_t *row = &buffer[y * view_width];
                    for (int x = x_start; x < x_end; x++) {
                        row[x] = fg_color;
                    }
                }
            }
        }
    } else {
#if PERF_ENABLED
        perf_skip_draws++;
#endif
    }
    // If scroll_delta == 0, no update needed (paused or same frame)
    
    // Draw Grid Lines (Beat Grid)
    if (beat_positions && num_beats > 0) {
        lv_color_t grid_color = lv_color_make(100, 100, 100); // Dim gray
        float px_per_sec = 172.26f; // 44100 / 256
        int center_x = view_width / 2;
        float time_window_half = center_x / px_per_sec;
        float start_time = precise_time - time_window_half;
        float end_time = precise_time + time_window_half;
        
        for (size_t i = 0; i < num_beats; i++) {
            float b = beat_positions[i];
            // Skip beats outside visible window
            if (b < start_time) continue;
            if (b > end_time) break;
            
            int x = center_x + (int)((b - precise_time) * px_per_sec);
            if (x >= 0 && x < (int)view_width) {
                // Draw vertical dotted line overlay
                for (int y = 0; y < (int)view_height; y += GRID_DOT_SPACING) {
                    if (y + 1 < (int)view_height) {
                        buffer[y * view_width + x] = grid_color;
                        buffer[(y+1) * view_width + x] = grid_color; 
                    }
                }
            }
        }
    }

#if PERF_ENABLED
    t_draw_end = perf_get_time_us();
    uint64_t t_invalidate_start = perf_get_time_us();
#endif
    
    lv_obj_invalidate(waveform_canvas);
    
    // Update last draw time for throttling
    last_draw_time_us = esp_timer_get_time();
    
#if PERF_ENABLED
    uint64_t t_invalidate_end = perf_get_time_us();
    uint64_t t_end = perf_get_time_us();
    
    // Record timing samples
    perf_frame_times[perf_index] = t_end - t_start;
    perf_cache_times[perf_index] = t_cache_end - t_cache_start;
    perf_draw_times[perf_index] = t_draw_end - t_draw_start;
    perf_invalidate_times[perf_index] = t_invalidate_end - t_invalidate_start;
    
    perf_index = (perf_index + 1) % PERF_ROLLING_WINDOW;
    perf_frame_count++;
    
    // Periodic stats logging
    if (perf_frame_count % PERF_LOG_INTERVAL == 0) {
        perf_log_stats();
    }
#endif
}

// Overview waveform stripe (bottom of waveform container)
static lv_obj_t *overview_container = NULL;
static lv_obj_t *overview_canvas = NULL;
static lv_obj_t *overview_position_line = NULL;
static uint8_t overview_waveform[WAVEFORM_BARS];
static bool overview_valid = false;

#define OVERVIEW_HEIGHT 20  // Height of overview stripe in pixels

/**
 * @brief Handle touch/click on overview bar for seek-to-position
 * 
 * When user taps the overview waveform stripe, seek to that position in the track.
 */
static void overview_click_handler(lv_event_t *e) {
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    
    // Get the overview canvas object
    lv_obj_t *target = lv_event_get_target(e);
    
    // Get absolute position of the overview canvas
    lv_coord_t canvas_x = 0;
    lv_obj_t *parent = target;
    while (parent != NULL) {
        canvas_x += lv_obj_get_x(parent);
        parent = lv_obj_get_parent(parent);
    }
    
    // Calculate local X coordinate relative to overview canvas
    lv_coord_t local_x = point.x - canvas_x;
    
    // Calculate position as percentage (0.0 to 1.0)
    float position = (float)local_x / (float)view_width;
    
    // Clamp to valid range
    if (position < 0.0f) position = 0.0f;
    if (position > 1.0f) position = 1.0f;
    
    ESP_LOGI(TAG, "Overview seek: touch_x=%d, local_x=%d, position=%.3f", 
             point.x, local_x, position);
    
    // Seek audio using VBR-aware seek table
    audio_player_seek_percent(position);
    
    // Update position indicator immediately for responsive feel
    if (overview_position_line) {
        int pos_x = (int)(position * (view_width - 2));
        if (pos_x < 0) pos_x = 0;
        if (pos_x > (int)(view_width - 2)) pos_x = view_width - 2;
        lv_obj_set_x(overview_position_line, pos_x);
    }
    
    // Force waveform to do a full redraw on next update (seek invalidates cache)
    first_frame = true;
    display_cache_valid = false;
}

/**
 * @brief Draw the static overview waveform on the overview canvas
 * 
 * Called once when track loads (not every frame).
 * Uses direct buffer access for performance.
 */
static void draw_overview_waveform(void) {
    if (!overview_canvas || !overview_valid) return;
    
    lv_img_dsc_t *canvas_img = lv_canvas_get_img(overview_canvas);
    if (!canvas_img) return;
    lv_color_t *buffer = (lv_color_t *)canvas_img->data;
    if (!buffer) return;
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    lv_color_t bg_color = lv_color_black();
    
    // Dim the overview waveform slightly (50% brightness)
    lv_color_t dim_fg = lv_color_make(
        lv_color_brightness(fg_color) / 2,
        lv_color_brightness(fg_color) / 2,
        lv_color_brightness(fg_color) / 2
    );
    // Actually, use the same foreground but at lower opacity effect
    dim_fg = lv_color_mix(fg_color, bg_color, 128); // 50% mix
    
    int center_y = OVERVIEW_HEIGHT / 2;
    
    // Clear buffer
    size_t buffer_size = view_width * OVERVIEW_HEIGHT;
    for (size_t i = 0; i < buffer_size; i++) {
        buffer[i] = bg_color;
    }
    
    // Draw overview waveform (1 pixel per sample)
    for (int x = 0; x < (int)view_width && x < WAVEFORM_BARS; x++) {
        uint8_t peak = overview_waveform[x];
        if (peak > 0) {
            // Scale peak to overview height
            int bar_height = (peak * (OVERVIEW_HEIGHT - 2)) / 255;
            if (bar_height < 1) bar_height = 1;
            
            int y_start = center_y - (bar_height / 2);
            int y_end = center_y + (bar_height / 2);
            if (y_start < 0) y_start = 0;
            if (y_end >= OVERVIEW_HEIGHT) y_end = OVERVIEW_HEIGHT - 1;
            
            for (int y = y_start; y <= y_end; y++) {
                buffer[y * view_width + x] = dim_fg;
            }
        }
    }
    
    lv_obj_invalidate(overview_canvas);
}

void waveform_view_init(uint32_t width, uint32_t height) {
    ESP_LOGI(TAG, "Waveform view initialized: %ux%u", width, height);
    
    view_width = width;
    
    // Calculate available height: Total - Top Bar (30) - Bottom Bar (40)
    int top_bar_height = 30;
    int bottom_bar_height = 40;
    int available_height = height - top_bar_height - bottom_bar_height;
    
    // PERFORMANCE OPTIMIZATION: Use smaller canvas (100 pixels = ~96KB buffer)
    // This is the main waveform display area, kept compact for faster rendering
    // The container is full height but canvas is centered within it
    int container_height = available_height;  // Container spans full available area
    view_height = 100;  // Reduced from 202 for ~2x performance improvement
    waveform_height = 80;  // 80% of view used for waveform bars
    
    int canvas_y_offset = (container_height - view_height) / 2;  // Center canvas vertically
    
    // Create container (full available height, black background)
    waveform_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(waveform_container, view_width, container_height);
    lv_obj_set_pos(waveform_container, 0, top_bar_height); 
    lv_obj_set_style_bg_color(waveform_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(waveform_container, 0, 0);
    lv_obj_set_style_pad_all(waveform_container, 0, 0);
    lv_obj_clear_flag(waveform_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create canvas for waveform (reduced height for performance)
    waveform_canvas = lv_canvas_create(waveform_container);
    
    size_t canvas_buf_size = view_width * view_height * sizeof(lv_color_t);
    ESP_LOGI(TAG, "Allocating canvas buffer: %zu bytes (height=%d, optimized)", 
             canvas_buf_size, (int)view_height);
    
    void *canvas_buf = NULL;
    
    // Try INTERNAL RAM first (96KB might fit!)
    canvas_buf = heap_caps_malloc(canvas_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!canvas_buf) {
        ESP_LOGW(TAG, "Internal RAM allocation failed, trying PSRAM...");
        canvas_buf = heap_caps_malloc(canvas_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        ESP_LOGI(TAG, "Canvas buffer allocated in INTERNAL RAM!");
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
        lv_obj_set_pos(waveform_canvas, 0, canvas_y_offset);  // Center vertically
        lv_canvas_fill_bg(waveform_canvas, lv_color_black(), LV_OPA_COVER);
    }
    
    // Create fixed playhead line (center, spans full container height)
    playhead_line = lv_obj_create(waveform_container);
    lv_obj_set_size(playhead_line, 2, container_height - OVERVIEW_HEIGHT);
    lv_obj_set_pos(playhead_line, view_width / 2 - 1, 0);
    lv_obj_set_style_bg_color(playhead_line, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(playhead_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(playhead_line, 0, 0);
    lv_obj_clear_flag(playhead_line, LV_OBJ_FLAG_CLICKABLE);
    
    // ========================================================================
    // Overview Waveform Stripe (bottom of container)
    // Shows full track structure with moving position indicator
    // ========================================================================
    overview_container = lv_obj_create(waveform_container);
    lv_obj_set_size(overview_container, view_width, OVERVIEW_HEIGHT);
    lv_obj_set_align(overview_container, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_color(overview_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(overview_container, 0, 0);
    lv_obj_set_style_pad_all(overview_container, 0, 0);
    lv_obj_clear_flag(overview_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create canvas for overview waveform
    overview_canvas = lv_canvas_create(overview_container);
    size_t overview_buf_size = view_width * OVERVIEW_HEIGHT * sizeof(lv_color_t);
    ESP_LOGI(TAG, "Allocating overview canvas: %zu bytes", overview_buf_size);
    
    void *overview_buf = heap_caps_malloc(overview_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!overview_buf) {
        ESP_LOGW(TAG, "Internal RAM failed for overview, trying PSRAM...");
        overview_buf = heap_caps_malloc(overview_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    
    if (overview_buf) {
        lv_canvas_set_buffer(overview_canvas, overview_buf, view_width, OVERVIEW_HEIGHT, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_size(overview_canvas, view_width, OVERVIEW_HEIGHT);
        lv_obj_set_pos(overview_canvas, 0, 0);
        lv_canvas_fill_bg(overview_canvas, lv_color_black(), LV_OPA_COVER);
        
        // Enable click events on overview canvas for seek-by-touch
        lv_obj_add_flag(overview_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overview_canvas, overview_click_handler, LV_EVENT_CLICKED, NULL);
        
        ESP_LOGI(TAG, "Overview canvas allocated at %p (touch-seek enabled)", overview_buf);
    } else {
        ESP_LOGE(TAG, "Failed to allocate overview canvas buffer");
        lv_obj_del(overview_canvas);
        overview_canvas = NULL;
    }
    
    // Position indicator line (moves across overview)
    overview_position_line = lv_obj_create(overview_container);
    lv_obj_set_size(overview_position_line, 2, OVERVIEW_HEIGHT);
    lv_obj_set_pos(overview_position_line, 0, 0);
    lv_obj_set_style_bg_color(overview_position_line, hud_theme_get_foreground_color(), 0);
    lv_obj_set_style_bg_opa(overview_position_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overview_position_line, 0, 0);
    lv_obj_clear_flag(overview_position_line, LV_OBJ_FLAG_CLICKABLE);
    
    // Initialize overview waveform data to zero
    memset(overview_waveform, 0, sizeof(overview_waveform));
    overview_valid = false;
    
    // Initially hidden
    lv_obj_add_flag(waveform_container, LV_OBJ_FLAG_HIDDEN);
}

void waveform_view_update(const uint8_t *waveform_data, 
                         size_t num_samples, 
                         float position,
                         float precise_time,
                         size_t wave_index) {
    if (!visible || !waveform_canvas) return;
    
    // Update overview position indicator (position 0.0 to 1.0 = start to end)
    if (overview_position_line) {
        int pos_x = (int)(position * (view_width - 2));
        if (pos_x < 0) pos_x = 0;
        if (pos_x > (int)(view_width - 2)) pos_x = view_width - 2;
        lv_obj_set_x(overview_position_line, pos_x);
    }
    
    // Draw waveform with ring buffer scroll optimization
    draw_waveform(waveform_data, num_samples, wave_index, precise_time);
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
    last_draw_time_us = 0;
    display_cache_valid = false;
    display_cache_center_index = 0;
    memset(display_cache, 0, sizeof(display_cache));
    
    // Reset overview waveform
    memset(overview_waveform, 0, sizeof(overview_waveform));
    overview_valid = false;
    
    // Clear overview canvas
    if (overview_canvas) {
        lv_canvas_fill_bg(overview_canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_invalidate(overview_canvas);
    }
    
    // Reset position indicator to start
    if (overview_position_line) {
        lv_obj_set_x(overview_position_line, 0);
    }
}

void waveform_view_set_overview(const uint8_t *data, size_t size) {
    if (!data || size == 0) {
        overview_valid = false;
        return;
    }
    
    // Copy overview data (up to WAVEFORM_BARS points)
    size_t copy_size = (size > WAVEFORM_BARS) ? WAVEFORM_BARS : size;
    memcpy(overview_waveform, data, copy_size);
    
    // Zero-pad if needed
    if (copy_size < WAVEFORM_BARS) {
        memset(overview_waveform + copy_size, 0, WAVEFORM_BARS - copy_size);
    }
    
    overview_valid = true;
    
    // Draw the overview waveform
    draw_overview_waveform();
    
    ESP_LOGI(TAG, "Overview waveform set (%zu points)", copy_size);
}

void waveform_view_set_resolution(int divider) {
    if (divider < 1) divider = 1;
    if (divider > 8) divider = 8;
    
    if (divider != resolution_divider) {
        resolution_divider = divider;
        first_frame = true;  // Force full redraw with new resolution
        ESP_LOGI(TAG, "Waveform resolution set to 1/%d (%d bars)", 
                 divider, WAVEFORM_BARS / divider);
    }
}

int waveform_view_get_resolution(void) {
    return resolution_divider;
}

// ============================================================================
// PERFORMANCE API
// ============================================================================

float waveform_view_get_fps(void) {
#if PERF_ENABLED
    if (perf_frame_count == 0) return 0.0f;
    
    int samples = (perf_frame_count < PERF_ROLLING_WINDOW) ? perf_frame_count : PERF_ROLLING_WINDOW;
    uint64_t total = 0;
    for (int i = 0; i < samples; i++) {
        total += perf_frame_times[i];
    }
    uint64_t avg = total / samples;
    return (avg > 0) ? 1000000.0f / avg : 0.0f;
#else
    return -1.0f;  // Profiling disabled
#endif
}

void waveform_view_get_perf_stats(uint32_t *frame_us, uint32_t *cache_us, 
                                   uint32_t *draw_us, uint32_t *invalidate_us) {
#if PERF_ENABLED
    if (perf_frame_count == 0) {
        if (frame_us) *frame_us = 0;
        if (cache_us) *cache_us = 0;
        if (draw_us) *draw_us = 0;
        if (invalidate_us) *invalidate_us = 0;
        return;
    }
    
    int samples = (perf_frame_count < PERF_ROLLING_WINDOW) ? perf_frame_count : PERF_ROLLING_WINDOW;
    uint64_t tf = 0, tc = 0, td = 0, ti = 0;
    for (int i = 0; i < samples; i++) {
        tf += perf_frame_times[i];
        tc += perf_cache_times[i];
        td += perf_draw_times[i];
        ti += perf_invalidate_times[i];
    }
    
    if (frame_us) *frame_us = (uint32_t)(tf / samples);
    if (cache_us) *cache_us = (uint32_t)(tc / samples);
    if (draw_us) *draw_us = (uint32_t)(td / samples);
    if (invalidate_us) *invalidate_us = (uint32_t)(ti / samples);
#else
    if (frame_us) *frame_us = 0;
    if (cache_us) *cache_us = 0;
    if (draw_us) *draw_us = 0;
    if (invalidate_us) *invalidate_us = 0;
#endif
}

void waveform_view_reset_perf(void) {
#if PERF_ENABLED
    perf_index = 0;
    perf_frame_count = 0;
    perf_full_redraws = 0;
    perf_incremental_draws = 0;
    perf_skip_draws = 0;
    memset(perf_frame_times, 0, sizeof(perf_frame_times));
    memset(perf_cache_times, 0, sizeof(perf_cache_times));
    memset(perf_draw_times, 0, sizeof(perf_draw_times));
    memset(perf_invalidate_times, 0, sizeof(perf_invalidate_times));
#endif
}

// ============================================================================
// NUDGE ANIMATION API
// ============================================================================

void waveform_view_trigger_nudge(void) {
    nudge_offset_px = NUDGE_AMOUNT_PX;
    nudge_start_time = esp_timer_get_time();
}

/**
 * @brief Calculate current nudge offset with ease-out decay
 * 
 * @return Current X offset in pixels (0 when animation complete)
 */
static int calculate_nudge_offset(void) {
    if (nudge_offset_px == 0) return 0;
    
    int64_t now = esp_timer_get_time();
    int64_t elapsed_us = now - nudge_start_time;
    int elapsed_ms = (int)(elapsed_us / 1000);
    
    if (elapsed_ms >= NUDGE_DURATION_MS) {
        // Animation complete
        nudge_offset_px = 0;
        return 0;
    }
    
    // Ease-out: offset = NUDGE_AMOUNT_PX * (1 - decay)^2
    float decay = (float)elapsed_ms / (float)NUDGE_DURATION_MS;
    float remaining = 1.0f - decay;
    int offset = (int)(NUDGE_AMOUNT_PX * remaining * remaining);
    
    return offset;
}