/**
 * @file cue_markers.c
 * @brief Cue markers UI overlay for waveform display
 * 
 * Renders colored triangular/diamond markers on the waveform to indicate
 * hot cues, memory cues, and loop regions. Optimized for ESP32-S3 with LVGL.
 */

#include "cue_markers.h"
#include "hud_theme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <math.h>

static const char *TAG = "cue_markers";

// ============================================================================
// CONFIGURATION
// ============================================================================

#define MARKER_HEIGHT           16      // Marker triangle height in pixels
#define MARKER_WIDTH            12      // Marker triangle base width in pixels
#define MARKER_LABEL_OFFSET     2       // Label offset from top
#define LOOP_OVERLAY_ALPHA      80      // Loop region overlay opacity (0-255)
#define TRIGGER_FLASH_DURATION_MS 150   // Animation flash duration
#define OVERVIEW_MARKER_HEIGHT  4       // Marker height in overview stripe

// Pixels per second at default zoom (from waveform_view.c)
#define PIXELS_PER_SECOND       172.26f

// ============================================================================
// STATE
// ============================================================================

static lv_obj_t *markers_container = NULL;
static lv_obj_t *markers_canvas = NULL;
static lv_obj_t *loop_overlay = NULL;

static uint32_t view_width = 0;
static uint32_t view_height = 0;
static bool visible = true;
static bool initialized = false;

// Hot cues (fixed slots)
static cue_marker_t hot_cues[CUE_MARKERS_MAX_HOT_CUES];

// Memory cues (dynamic)
static cue_marker_t memory_cues[CUE_MARKERS_MAX_MEMORY_CUES];
static int memory_cue_count = 0;

// Loop region
static cue_loop_region_t loop_region;

// Current playback state (for marker positioning)
static float current_playback_position = 0.0f;
static float current_playback_seconds = 0.0f;
static float current_zoom = 1.0f;
static float current_center_time = 0.0f;

// Selection state
static int selected_marker_index = -1;
static cue_marker_type_t selected_marker_type = CUE_TYPE_HOT_CUE;

// ============================================================================
// COLOR PRESETS
// ============================================================================

// CDJ-style hot cue colors
static const uint32_t color_presets[] = {
    0xE53935,   // CUE_COLOR_RED
    0xFF9800,   // CUE_COLOR_ORANGE
    0xFFEB3B,   // CUE_COLOR_YELLOW
    0x4CAF50,   // CUE_COLOR_GREEN
    0x00BCD4,   // CUE_COLOR_CYAN
    0x2196F3,   // CUE_COLOR_BLUE
    0x9C27B0,   // CUE_COLOR_PURPLE
    0xE91E63,   // CUE_COLOR_PINK
    0xFFFFFF,   // CUE_COLOR_WHITE
};

// Default hot cue colors (cycle through)
static const cue_marker_color_t default_hot_cue_colors[] = {
    CUE_COLOR_RED,
    CUE_COLOR_ORANGE,
    CUE_COLOR_YELLOW,
    CUE_COLOR_GREEN,
    CUE_COLOR_CYAN,
    CUE_COLOR_BLUE,
    CUE_COLOR_PURPLE,
    CUE_COLOR_PINK,
};

lv_color_t cue_markers_get_preset_color(cue_marker_color_t preset) {
    if (preset == CUE_COLOR_CUSTOM || preset > CUE_COLOR_WHITE) {
        return lv_color_white();
    }
    uint32_t rgb = color_presets[preset];
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// ============================================================================
// DRAWING UTILITIES
// ============================================================================

/**
 * @brief Draw a triangular marker pointing down at position
 */
static void draw_triangle_marker(lv_color_t *buffer, int x, int y, 
                                  lv_color_t color, bool selected, bool triggered) {
    if (x < 0 || x >= (int)view_width) return;
    
    // Apply brightness boost if triggered (flash effect)
    lv_color_t draw_color = color;
    if (triggered) {
        draw_color = lv_color_white();  // Flash white
    } else if (selected) {
        // Brighten selected markers
        draw_color = lv_color_mix(color, lv_color_white(), 180);
    }
    
    // Draw filled triangle pointing down
    // Apex at (x, y + MARKER_HEIGHT), base from (x - MARKER_WIDTH/2, y) to (x + MARKER_WIDTH/2, y)
    int half_width = MARKER_WIDTH / 2;
    
    for (int row = 0; row < MARKER_HEIGHT; row++) {
        // Calculate width at this row (narrows as we go down)
        int row_half_width = half_width - (row * half_width / MARKER_HEIGHT);
        int y_pos = y + row;
        
        if (y_pos < 0 || y_pos >= (int)view_height) continue;
        
        int x_start = x - row_half_width;
        int x_end = x + row_half_width;
        
        if (x_start < 0) x_start = 0;
        if (x_end >= (int)view_width) x_end = view_width - 1;
        
        for (int col = x_start; col <= x_end; col++) {
            buffer[y_pos * view_width + col] = draw_color;
        }
    }
    
    // Draw outline for better visibility
    lv_color_t outline_color = lv_color_black();
    
    // Left edge
    for (int row = 0; row < MARKER_HEIGHT; row++) {
        int row_half_width = half_width - (row * half_width / MARKER_HEIGHT);
        int y_pos = y + row;
        int x_left = x - row_half_width - 1;
        
        if (y_pos >= 0 && y_pos < (int)view_height && x_left >= 0) {
            buffer[y_pos * view_width + x_left] = outline_color;
        }
    }
    
    // Right edge
    for (int row = 0; row < MARKER_HEIGHT; row++) {
        int row_half_width = half_width - (row * half_width / MARKER_HEIGHT);
        int y_pos = y + row;
        int x_right = x + row_half_width + 1;
        
        if (y_pos >= 0 && y_pos < (int)view_height && x_right < (int)view_width) {
            buffer[y_pos * view_width + x_right] = outline_color;
        }
    }
}

/**
 * @brief Draw a diamond marker (for memory cues)
 */
static void draw_diamond_marker(lv_color_t *buffer, int x, int y,
                                 lv_color_t color, bool selected) {
    if (x < 0 || x >= (int)view_width) return;
    
    lv_color_t draw_color = selected ? lv_color_mix(color, lv_color_white(), 180) : color;
    
    int half_size = MARKER_HEIGHT / 2;
    
    // Top half of diamond
    for (int row = 0; row < half_size; row++) {
        int row_half_width = row;
        int y_pos = y + row;
        
        if (y_pos < 0 || y_pos >= (int)view_height) continue;
        
        int x_start = x - row_half_width;
        int x_end = x + row_half_width;
        
        if (x_start < 0) x_start = 0;
        if (x_end >= (int)view_width) x_end = view_width - 1;
        
        for (int col = x_start; col <= x_end; col++) {
            buffer[y_pos * view_width + col] = draw_color;
        }
    }
    
    // Bottom half of diamond
    for (int row = 0; row < half_size; row++) {
        int row_half_width = half_size - row - 1;
        int y_pos = y + half_size + row;
        
        if (y_pos < 0 || y_pos >= (int)view_height) continue;
        
        int x_start = x - row_half_width;
        int x_end = x + row_half_width;
        
        if (x_start < 0) x_start = 0;
        if (x_end >= (int)view_width) x_end = view_width - 1;
        
        for (int col = x_start; col <= x_end; col++) {
            buffer[y_pos * view_width + col] = draw_color;
        }
    }
}

/**
 * @brief Draw vertical line for loop boundary
 */
static void draw_loop_boundary(lv_color_t *buffer, int x, lv_color_t color, bool is_start) {
    if (x < 0 || x >= (int)view_width) return;
    
    // Draw vertical line with bracket
    for (int y = 0; y < (int)view_height; y++) {
        buffer[y * view_width + x] = color;
    }
    
    // Draw horizontal bracket at top and bottom
    int bracket_len = 8;
    int dir = is_start ? 1 : -1;
    
    // Top bracket
    for (int i = 0; i < bracket_len; i++) {
        int bx = x + (i * dir);
        if (bx >= 0 && bx < (int)view_width) {
            buffer[bx] = color;  // y=0
        }
    }
    
    // Bottom bracket
    for (int i = 0; i < bracket_len; i++) {
        int bx = x + (i * dir);
        if (bx >= 0 && bx < (int)view_width) {
            buffer[(view_height - 1) * view_width + bx] = color;
        }
    }
}

/**
 * @brief Calculate X position for a marker given current view parameters
 * 
 * @param marker_seconds Marker position in seconds
 * @return X coordinate, or -1 if off-screen
 */
static int calculate_marker_x(float marker_seconds) {
    // Calculate time window based on zoom
    float time_window_half = (view_width / 2) / (PIXELS_PER_SECOND * current_zoom);
    float start_time = current_center_time - time_window_half;
    float end_time = current_center_time + time_window_half;
    
    // Check if marker is visible
    if (marker_seconds < start_time || marker_seconds > end_time) {
        return -1;  // Off-screen
    }
    
    // Calculate X position
    int center_x = view_width / 2;
    float time_offset = marker_seconds - current_center_time;
    int x = center_x + (int)(time_offset * PIXELS_PER_SECOND * current_zoom);
    
    return x;
}

/**
 * @brief Check and clear trigger animation state
 */
static void update_trigger_animations(void) {
    int64_t now = esp_timer_get_time() / 1000;  // ms
    
    for (int i = 0; i < CUE_MARKERS_MAX_HOT_CUES; i++) {
        if (hot_cues[i].triggered) {
            // Clear after flash duration (simple timeout, no stored time)
            hot_cues[i].triggered = false;
        }
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void cue_markers_init(lv_obj_t *parent, uint32_t width, uint32_t height) {
    if (initialized) {
        ESP_LOGW(TAG, "Cue markers already initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing cue markers: %ux%u", width, height);
    
    view_width = width;
    view_height = height;
    
    // Create transparent overlay container on top of waveform
    markers_container = lv_obj_create(parent);
    lv_obj_set_size(markers_container, width, height);
    lv_obj_set_pos(markers_container, 0, 0);
    lv_obj_set_style_bg_opa(markers_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(markers_container, 0, 0);
    lv_obj_set_style_pad_all(markers_container, 0, 0);
    lv_obj_clear_flag(markers_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(markers_container, LV_OBJ_FLAG_CLICKABLE);
    
    // Create canvas for marker drawing
    markers_canvas = lv_canvas_create(markers_container);
    
    // Allocate canvas buffer (ARGB8888 for transparency support)
    // Note: Using smaller height for markers only at top of view
    uint32_t canvas_height = MARKER_HEIGHT + 4;  // Just enough for markers
    size_t buf_size = width * canvas_height * sizeof(lv_color_t);
    
    void *canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!canvas_buf) {
        ESP_LOGW(TAG, "Internal RAM failed, trying PSRAM");
        canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    
    if (canvas_buf) {
        lv_canvas_set_buffer(markers_canvas, canvas_buf, width, canvas_height, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_size(markers_canvas, width, canvas_height);
        lv_obj_set_pos(markers_canvas, 0, 0);  // Top of view
        lv_canvas_fill_bg(markers_canvas, lv_color_black(), LV_OPA_TRANSP);
        ESP_LOGI(TAG, "Marker canvas allocated: %zu bytes", buf_size);
    } else {
        ESP_LOGE(TAG, "Failed to allocate marker canvas");
        lv_obj_del(markers_canvas);
        markers_canvas = NULL;
    }
    
    // Initialize marker arrays
    memset(hot_cues, 0, sizeof(hot_cues));
    memset(memory_cues, 0, sizeof(memory_cues));
    memset(&loop_region, 0, sizeof(loop_region));
    memory_cue_count = 0;
    
    initialized = true;
    ESP_LOGI(TAG, "Cue markers initialized");
}

void cue_markers_deinit(void) {
    if (!initialized) return;
    
    if (markers_canvas) {
        lv_img_dsc_t *img = lv_canvas_get_img(markers_canvas);
        if (img && img->data) {
            heap_caps_free((void*)img->data);
        }
        lv_obj_del(markers_canvas);
        markers_canvas = NULL;
    }
    
    if (markers_container) {
        lv_obj_del(markers_container);
        markers_container = NULL;
    }
    
    initialized = false;
    ESP_LOGI(TAG, "Cue markers deinitialized");
}

void cue_markers_reset(void) {
    // Clear all markers
    memset(hot_cues, 0, sizeof(hot_cues));
    memset(memory_cues, 0, sizeof(memory_cues));
    memset(&loop_region, 0, sizeof(loop_region));
    memory_cue_count = 0;
    selected_marker_index = -1;
    
    // Clear canvas
    if (markers_canvas) {
        lv_canvas_fill_bg(markers_canvas, lv_color_black(), LV_OPA_TRANSP);
        lv_obj_invalidate(markers_canvas);
    }
    
    ESP_LOGI(TAG, "Cue markers reset");
}

// ============================================================================
// HOT CUE MANAGEMENT
// ============================================================================

bool cue_markers_set_hot_cue(int slot, float position, float position_seconds,
                              cue_marker_color_t color, const char *label) {
    if (slot < 0 || slot >= CUE_MARKERS_MAX_HOT_CUES) {
        ESP_LOGW(TAG, "Invalid hot cue slot: %d", slot);
        return false;
    }
    
    cue_marker_t *cue = &hot_cues[slot];
    cue->active = true;
    cue->position = position;
    cue->position_seconds = position_seconds;
    cue->type = CUE_TYPE_HOT_CUE;
    cue->color_preset = (color == CUE_COLOR_CUSTOM) ? default_hot_cue_colors[slot % 8] : color;
    cue->selected = false;
    cue->triggered = false;
    
    // Set label
    if (label && label[0]) {
        strncpy(cue->label, label, CUE_MARKERS_LABEL_MAX_LEN);
        cue->label[CUE_MARKERS_LABEL_MAX_LEN] = '\0';
    } else {
        // Default numeric label
        snprintf(cue->label, sizeof(cue->label), "%d", slot + 1);
    }
    
    ESP_LOGI(TAG, "Hot cue %d set at %.2fs (pos=%.4f)", slot, position_seconds, position);
    return true;
}

void cue_markers_clear_hot_cue(int slot) {
    if (slot < 0 || slot >= CUE_MARKERS_MAX_HOT_CUES) return;
    
    hot_cues[slot].active = false;
    ESP_LOGI(TAG, "Hot cue %d cleared", slot);
}

const cue_marker_t* cue_markers_get_hot_cue(int slot) {
    if (slot < 0 || slot >= CUE_MARKERS_MAX_HOT_CUES) return NULL;
    if (!hot_cues[slot].active) return NULL;
    return &hot_cues[slot];
}

void cue_markers_trigger_hot_cue(int slot) {
    if (slot < 0 || slot >= CUE_MARKERS_MAX_HOT_CUES) return;
    if (!hot_cues[slot].active) return;
    
    hot_cues[slot].triggered = true;
    cue_markers_invalidate();
}

// ============================================================================
// MEMORY CUE MANAGEMENT
// ============================================================================

int cue_markers_add_memory_cue(float position, float position_seconds, 
                                const char *label) {
    if (memory_cue_count >= CUE_MARKERS_MAX_MEMORY_CUES) {
        ESP_LOGW(TAG, "Memory cue slots full");
        return -1;
    }
    
    // Find first empty slot
    int slot = -1;
    for (int i = 0; i < CUE_MARKERS_MAX_MEMORY_CUES; i++) {
        if (!memory_cues[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) return -1;
    
    cue_marker_t *cue = &memory_cues[slot];
    cue->active = true;
    cue->position = position;
    cue->position_seconds = position_seconds;
    cue->type = CUE_TYPE_MEMORY_CUE;
    cue->color_preset = CUE_COLOR_WHITE;
    cue->selected = false;
    cue->triggered = false;
    
    if (label && label[0]) {
        strncpy(cue->label, label, CUE_MARKERS_LABEL_MAX_LEN);
        cue->label[CUE_MARKERS_LABEL_MAX_LEN] = '\0';
    } else {
        cue->label[0] = '\0';
    }
    
    memory_cue_count++;
    ESP_LOGI(TAG, "Memory cue added at slot %d (%.2fs)", slot, position_seconds);
    return slot;
}

void cue_markers_remove_memory_cue(int index) {
    if (index < 0 || index >= CUE_MARKERS_MAX_MEMORY_CUES) return;
    if (!memory_cues[index].active) return;
    
    memory_cues[index].active = false;
    memory_cue_count--;
    ESP_LOGI(TAG, "Memory cue %d removed", index);
}

int cue_markers_get_memory_cue_count(void) {
    return memory_cue_count;
}

// ============================================================================
// LOOP MARKERS
// ============================================================================

void cue_markers_set_loop(float start_position, float end_position,
                          float start_seconds, float end_seconds) {
    loop_region.active = true;
    loop_region.start_position = start_position;
    loop_region.end_position = end_position;
    loop_region.start_seconds = start_seconds;
    loop_region.end_seconds = end_seconds;
    loop_region.enabled = false;
    
    ESP_LOGI(TAG, "Loop set: %.2fs - %.2fs", start_seconds, end_seconds);
}

void cue_markers_clear_loop(void) {
    loop_region.active = false;
    loop_region.enabled = false;
    ESP_LOGI(TAG, "Loop cleared");
}

void cue_markers_set_loop_enabled(bool enabled) {
    if (loop_region.active) {
        loop_region.enabled = enabled;
        ESP_LOGI(TAG, "Loop %s", enabled ? "enabled" : "disabled");
    }
}

const cue_loop_region_t* cue_markers_get_loop(void) {
    if (!loop_region.active) return NULL;
    return &loop_region;
}

// ============================================================================
// RENDERING
// ============================================================================

void cue_markers_update(float playback_position, float playback_seconds,
                        float zoom_level, float center_time) {
    if (!initialized || !visible || !markers_canvas) return;
    
    // Update state
    current_playback_position = playback_position;
    current_playback_seconds = playback_seconds;
    current_zoom = zoom_level;
    current_center_time = center_time;
    
    // Update trigger animations
    update_trigger_animations();
    
    // Get canvas buffer
    lv_img_dsc_t *canvas_img = lv_canvas_get_img(markers_canvas);
    if (!canvas_img) return;
    lv_color_t *buffer = (lv_color_t *)canvas_img->data;
    if (!buffer) return;
    
    uint32_t canvas_height = MARKER_HEIGHT + 4;
    
    // Clear canvas (transparent black)
    size_t buf_pixels = view_width * canvas_height;
    for (size_t i = 0; i < buf_pixels; i++) {
        buffer[i] = lv_color_black();
    }
    
    // Draw hot cues (triangles)
    for (int i = 0; i < CUE_MARKERS_MAX_HOT_CUES; i++) {
        if (!hot_cues[i].active) continue;
        
        int x = calculate_marker_x(hot_cues[i].position_seconds);
        if (x < 0) continue;  // Off-screen
        
        lv_color_t color = cue_markers_get_preset_color(hot_cues[i].color_preset);
        draw_triangle_marker(buffer, x, 2, color, 
                            hot_cues[i].selected, hot_cues[i].triggered);
    }
    
    // Draw memory cues (diamonds, smaller)
    for (int i = 0; i < CUE_MARKERS_MAX_MEMORY_CUES; i++) {
        if (!memory_cues[i].active) continue;
        
        int x = calculate_marker_x(memory_cues[i].position_seconds);
        if (x < 0) continue;  // Off-screen
        
        draw_diamond_marker(buffer, x, 4, lv_color_white(), memory_cues[i].selected);
    }
    
    // Draw loop markers
    if (loop_region.active) {
        lv_color_t loop_in_color = cue_markers_get_preset_color(CUE_COLOR_GREEN);
        lv_color_t loop_out_color = cue_markers_get_preset_color(CUE_COLOR_YELLOW);
        
        // Dim if loop not enabled
        if (!loop_region.enabled) {
            loop_in_color = lv_color_mix(loop_in_color, lv_color_black(), 128);
            loop_out_color = lv_color_mix(loop_out_color, lv_color_black(), 128);
        }
        
        int x_start = calculate_marker_x(loop_region.start_seconds);
        int x_end = calculate_marker_x(loop_region.end_seconds);
        
        // Draw loop in marker
        if (x_start >= 0) {
            draw_loop_boundary(buffer, x_start, loop_in_color, true);
        }
        
        // Draw loop out marker
        if (x_end >= 0) {
            draw_loop_boundary(buffer, x_end, loop_out_color, false);
        }
        
        // Draw shaded region between loop points if both visible
        if (x_start >= 0 && x_end >= 0 && x_start < x_end) {
            lv_color_t shade_color = loop_region.enabled ? 
                lv_color_make(0, 60, 0) : lv_color_make(30, 30, 30);
            
            for (int x = x_start + 1; x < x_end; x++) {
                for (uint32_t y = 0; y < canvas_height; y++) {
                    // Blend with existing
                    buffer[y * view_width + x] = lv_color_mix(
                        buffer[y * view_width + x], shade_color, 200);
                }
            }
        }
    }
    
    lv_obj_invalidate(markers_canvas);
}

void cue_markers_invalidate(void) {
    if (markers_canvas) {
        // Re-render with current state
        cue_markers_update(current_playback_position, current_playback_seconds,
                          current_zoom, current_center_time);
    }
}

void cue_markers_set_visible(bool vis) {
    visible = vis;
    if (markers_container) {
        if (visible) {
            lv_obj_clear_flag(markers_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(markers_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ============================================================================
// SELECTION / INTERACTION
// ============================================================================

int cue_markers_select_at_position(float position, float tolerance) {
    // Clear previous selection
    cue_markers_clear_selection();
    
    // Search hot cues first (higher priority)
    for (int i = 0; i < CUE_MARKERS_MAX_HOT_CUES; i++) {
        if (!hot_cues[i].active) continue;
        
        float diff = fabsf(hot_cues[i].position - position);
        if (diff <= tolerance) {
            hot_cues[i].selected = true;
            selected_marker_index = i;
            selected_marker_type = CUE_TYPE_HOT_CUE;
            cue_markers_invalidate();
            return i;
        }
    }
    
    // Search memory cues
    for (int i = 0; i < CUE_MARKERS_MAX_MEMORY_CUES; i++) {
        if (!memory_cues[i].active) continue;
        
        float diff = fabsf(memory_cues[i].position - position);
        if (diff <= tolerance) {
            memory_cues[i].selected = true;
            selected_marker_index = i;
            selected_marker_type = CUE_TYPE_MEMORY_CUE;
            cue_markers_invalidate();
            return i + CUE_MARKERS_MAX_HOT_CUES;  // Offset to distinguish
        }
    }
    
    return -1;  // No marker found
}

void cue_markers_clear_selection(void) {
    for (int i = 0; i < CUE_MARKERS_MAX_HOT_CUES; i++) {
        hot_cues[i].selected = false;
    }
    for (int i = 0; i < CUE_MARKERS_MAX_MEMORY_CUES; i++) {
        memory_cues[i].selected = false;
    }
    selected_marker_index = -1;
}

const cue_marker_t* cue_markers_get_selected(void) {
    if (selected_marker_index < 0) return NULL;
    
    if (selected_marker_type == CUE_TYPE_HOT_CUE) {
        return &hot_cues[selected_marker_index];
    } else if (selected_marker_type == CUE_TYPE_MEMORY_CUE) {
        return &memory_cues[selected_marker_index];
    }
    
    return NULL;
}
