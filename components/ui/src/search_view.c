/**
 * @file search_view.c
 * @brief Search view for library filtering with text search, BPM, and key filters
 * 
 * Features:
 * - Text search across title and artist
 * - BPM range filter with slider
 * - Key compatibility filter (Camelot wheel)
 * - On-screen keyboard using LVGL keyboard widget
 */

#include "search_view.h"
#include "hud_theme.h"
#include "track_db.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "search_view";

// ============================================================================
// Constants
// ============================================================================

#define MAX_SEARCH_TEXT 64
#define MAX_RESULTS 50
#define BPM_RANGE_DEFAULT 10.0f  // +/- 10 BPM from reference

// Camelot key names (same order as track_db)
static const char* const KEY_NAMES[] = {
    "1A", "2A", "3A", "4A", "5A", "6A", "7A", "8A", "9A", "10A", "11A", "12A",
    "1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B", "10B", "11B", "12B"
};

// ============================================================================
// UI State
// ============================================================================

static lv_obj_t *s_container = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_search_input = NULL;
static lv_obj_t *s_keyboard = NULL;
static lv_obj_t *s_filter_panel = NULL;
static lv_obj_t *s_bpm_slider = NULL;
static lv_obj_t *s_bpm_label = NULL;
static lv_obj_t *s_key_filter_btn = NULL;
static lv_obj_t *s_key_filter_label = NULL;
static lv_obj_t *s_results_list = NULL;
static lv_obj_t *s_status_label = NULL;

static bool s_visible = false;
static bool s_keyboard_visible = false;

// Filter state
static char s_search_text[MAX_SEARCH_TEXT] = {0};
static float s_bpm_min = 0.0f;
static float s_bpm_max = 999.0f;
static uint8_t s_reference_key = 255;  // 255 = no filter
static float s_reference_bpm = 0.0f;
static bool s_key_filter_enabled = false;

// Results
typedef struct {
    int db_index;       // Index in track database
    char display[128];  // Formatted display string
} search_result_t;

static search_result_t s_results[MAX_RESULTS];
static size_t s_result_count = 0;
static int s_selected_index = -1;

// List items
static lv_obj_t **s_list_items = NULL;
static size_t s_list_item_count = 0;

// Callbacks
static search_track_selected_cb s_track_cb = NULL;
static void *s_track_cb_data = NULL;
static search_back_cb s_back_cb = NULL;
static void *s_back_cb_data = NULL;

// Styles
static lv_style_t s_style_normal;
static lv_style_t s_style_selected;
static lv_style_t s_style_filter;
static bool s_styles_initialized = false;

// ============================================================================
// Forward Declarations
// ============================================================================

static void init_styles(void);
static void apply_filters(void);
static void update_results_display(void);
static void update_selection(int new_index);
static void free_list_items(void);
static bool is_key_compatible(uint8_t key_a, uint8_t key_b);
static bool match_search_text(const char *title, const char *artist, const char *query);
static void search_input_event_cb(lv_event_t *e);
static void keyboard_event_cb(lv_event_t *e);
static void bpm_slider_event_cb(lv_event_t *e);
static void key_filter_event_cb(lv_event_t *e);
static void result_item_clicked(lv_event_t *e);
static void show_keyboard(void);
static void hide_keyboard(void);
static void update_bpm_label(void);
static void update_key_filter_label(void);

// ============================================================================
// Style Initialization
// ============================================================================

static void init_styles(void) {
    if (s_styles_initialized) return;
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    
    // Normal style
    lv_style_init(&s_style_normal);
    lv_style_set_text_color(&s_style_normal, fg_color);
    lv_style_set_bg_color(&s_style_normal, lv_color_black());
    lv_style_set_bg_opa(&s_style_normal, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_style_normal, 0);
    lv_style_set_pad_all(&s_style_normal, 4);
    
    // Selected style (inverted)
    lv_style_init(&s_style_selected);
    lv_style_set_text_color(&s_style_selected, lv_color_black());
    lv_style_set_bg_color(&s_style_selected, fg_color);
    lv_style_set_bg_opa(&s_style_selected, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_selected, 0);
    lv_style_set_pad_all(&s_style_selected, 4);
    
    // Filter control style
    lv_style_init(&s_style_filter);
    lv_style_set_text_color(&s_style_filter, fg_color);
    lv_style_set_bg_color(&s_style_filter, lv_color_make(32, 32, 32));
    lv_style_set_bg_opa(&s_style_filter, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_filter, fg_color);
    lv_style_set_border_width(&s_style_filter, 1);
    lv_style_set_radius(&s_style_filter, 4);
    lv_style_set_pad_all(&s_style_filter, 4);
    
    s_styles_initialized = true;
}

// ============================================================================
// Key Compatibility (Camelot Wheel)
// ============================================================================

/**
 * @brief Check if two keys are harmonically compatible
 * 
 * Compatible keys on the Camelot wheel:
 * - Same key
 * - +1 or -1 on same letter (e.g., 8A -> 7A, 8A -> 9A)
 * - Same number, different letter (e.g., 8A -> 8B)
 */
static bool is_key_compatible(uint8_t key_a, uint8_t key_b) {
    // Unknown keys are not compatible
    if (key_a >= 24 || key_b >= 24) {
        return false;
    }
    
    // Same key is always compatible
    if (key_a == key_b) {
        return true;
    }
    
    // Extract number (1-12) and mode (A=0, B=1)
    int num_a = (key_a % 12) + 1;  // 1-12
    int mode_a = key_a / 12;        // 0=A, 1=B
    int num_b = (key_b % 12) + 1;
    int mode_b = key_b / 12;
    
    // Same mode: check if adjacent on wheel (+1 or -1, wrapping 12<->1)
    if (mode_a == mode_b) {
        int diff = abs(num_a - num_b);
        if (diff == 1 || diff == 11) {  // 11 = wrap around (1 and 12)
            return true;
        }
    }
    
    // Different mode: same number is compatible (relative major/minor)
    if (mode_a != mode_b && num_a == num_b) {
        return true;
    }
    
    return false;
}

/**
 * @brief Get list of compatible keys for display
 */
static void get_compatible_keys_str(uint8_t ref_key, char *buf, size_t buf_size) {
    if (ref_key >= 24) {
        snprintf(buf, buf_size, "All Keys");
        return;
    }
    
    int num = (ref_key % 12) + 1;
    int mode = ref_key / 12;
    
    // Calculate compatible keys
    int prev_num = (num == 1) ? 12 : num - 1;
    int next_num = (num == 12) ? 1 : num + 1;
    char mode_char = mode ? 'B' : 'A';
    char other_mode = mode ? 'A' : 'B';
    
    snprintf(buf, buf_size, "%d%c %d%c %d%c %d%c",
             num, mode_char,          // Same key
             prev_num, mode_char,     // -1
             next_num, mode_char,     // +1
             num, other_mode);        // Relative major/minor
}

// ============================================================================
// Text Search
// ============================================================================

/**
 * @brief Case-insensitive substring search
 */
static bool match_search_text(const char *title, const char *artist, const char *query) {
    if (!query || query[0] == '\0') {
        return true;  // Empty query matches everything
    }
    
    // Create lowercase copies for comparison
    char title_lower[MAX_TITLE_LEN];
    char artist_lower[MAX_ARTIST_LEN];
    char query_lower[MAX_SEARCH_TEXT];
    
    // Convert title to lowercase
    size_t i;
    for (i = 0; title && title[i] && i < sizeof(title_lower) - 1; i++) {
        title_lower[i] = tolower((unsigned char)title[i]);
    }
    title_lower[i] = '\0';
    
    // Convert artist to lowercase
    for (i = 0; artist && artist[i] && i < sizeof(artist_lower) - 1; i++) {
        artist_lower[i] = tolower((unsigned char)artist[i]);
    }
    artist_lower[i] = '\0';
    
    // Convert query to lowercase
    for (i = 0; query[i] && i < sizeof(query_lower) - 1; i++) {
        query_lower[i] = tolower((unsigned char)query[i]);
    }
    query_lower[i] = '\0';
    
    // Search in title and artist
    if (strstr(title_lower, query_lower) != NULL) {
        return true;
    }
    if (strstr(artist_lower, query_lower) != NULL) {
        return true;
    }
    
    return false;
}

// ============================================================================
// Filter Logic
// ============================================================================

// Disable format-truncation warning - snprintf safely truncates
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static void apply_filters(void) {
    s_result_count = 0;
    
    uint32_t track_count = track_db_get_count();
    
    for (uint32_t i = 0; i < track_count && s_result_count < MAX_RESULTS; i++) {
        track_info_t info;
        if (!track_db_get_track(i, &info)) {
            continue;
        }
        
        // Text filter
        if (!match_search_text(info.title, info.artist, s_search_text)) {
            continue;
        }
        
        // BPM filter
        if (s_bpm_min > 0 || s_bpm_max < 999) {
            if (info.bpm < s_bpm_min || info.bpm > s_bpm_max) {
                continue;
            }
        }
        
        // Key compatibility filter
        if (s_key_filter_enabled && s_reference_key < 24) {
            if (!is_key_compatible(s_reference_key, info.key_id)) {
                continue;
            }
        }
        
        // Track matches all filters - add to results
        search_result_t *result = &s_results[s_result_count];
        result->db_index = (int)i;
        
        // Format display string: "Title - Artist [BPM] [Key]"
        const char *display_title = (info.title[0] != '\0') ? info.title : info.filename;
        const char *key_str = (info.key_id < 24) ? KEY_NAMES[info.key_id] : "";
        
        if (info.artist[0] != '\0') {
            if (info.bpm > 0 && info.key_id < 24) {
                snprintf(result->display, sizeof(result->display), 
                         "%s - %s [%.0f] [%s]", 
                         display_title, info.artist, info.bpm, key_str);
            } else if (info.bpm > 0) {
                snprintf(result->display, sizeof(result->display), 
                         "%s - %s [%.0f]", 
                         display_title, info.artist, info.bpm);
            } else {
                snprintf(result->display, sizeof(result->display), 
                         "%s - %s", display_title, info.artist);
            }
        } else {
            if (info.bpm > 0 && info.key_id < 24) {
                snprintf(result->display, sizeof(result->display), 
                         "%s [%.0f] [%s]", display_title, info.bpm, key_str);
            } else if (info.bpm > 0) {
                snprintf(result->display, sizeof(result->display), 
                         "%s [%.0f]", display_title, info.bpm);
            } else {
                snprintf(result->display, sizeof(result->display), 
                         "%s", display_title);
            }
        }
        
        s_result_count++;
    }
    
    ESP_LOGI(TAG, "Filter applied: %zu results (text='%s', bpm=%.0f-%.0f, key=%s)",
             s_result_count, s_search_text, s_bpm_min, s_bpm_max,
             s_key_filter_enabled ? KEY_NAMES[s_reference_key] : "off");
    
    update_results_display();
}

#pragma GCC diagnostic pop

// ============================================================================
// Results Display
// ============================================================================

static void free_list_items(void) {
    if (s_list_items) {
        free(s_list_items);
        s_list_items = NULL;
    }
    s_list_item_count = 0;
    s_selected_index = -1;
}

static void update_results_display(void) {
    if (!s_results_list) return;
    
    // Clear existing items
    free_list_items();
    lv_obj_clean(s_results_list);
    
    // Update status label
    char status[64];
    snprintf(status, sizeof(status), "%zu tracks found", s_result_count);
    if (s_status_label) {
        lv_label_set_text(s_status_label, status);
    }
    
    if (s_result_count == 0) {
        lv_obj_t *btn = lv_list_add_btn(s_results_list, NULL, "No matching tracks");
        lv_obj_add_style(btn, &s_style_normal, 0);
        lv_obj_set_style_text_color(btn, lv_color_make(128, 128, 128), 0);
        return;
    }
    
    // Allocate item array
    s_list_items = (lv_obj_t**)calloc(s_result_count, sizeof(lv_obj_t*));
    if (!s_list_items) {
        ESP_LOGE(TAG, "Failed to allocate list items");
        return;
    }
    s_list_item_count = s_result_count;
    
    // Create list items
    for (size_t i = 0; i < s_result_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(s_results_list, LV_SYMBOL_AUDIO, s_results[i].display);
        lv_obj_add_style(btn, &s_style_normal, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(btn, result_item_clicked, LV_EVENT_CLICKED, NULL);
        s_list_items[i] = btn;
    }
    
    // Select first item
    if (s_list_item_count > 0) {
        update_selection(0);
    }
}

static void update_selection(int new_index) {
    if (!s_list_items || new_index < 0 || new_index >= (int)s_list_item_count) {
        return;
    }
    
    // Remove selection from current item
    if (s_selected_index >= 0 && s_selected_index < (int)s_list_item_count && 
        s_list_items[s_selected_index]) {
        lv_obj_remove_style(s_list_items[s_selected_index], &s_style_selected, 0);
        lv_obj_add_style(s_list_items[s_selected_index], &s_style_normal, 0);
    }
    
    // Apply selection to new item
    s_selected_index = new_index;
    if (s_list_items[s_selected_index]) {
        lv_obj_remove_style(s_list_items[s_selected_index], &s_style_normal, 0);
        lv_obj_add_style(s_list_items[s_selected_index], &s_style_selected, 0);
        lv_obj_scroll_to_view(s_list_items[s_selected_index], LV_ANIM_ON);
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

static void search_input_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        show_keyboard();
    }
}

static void keyboard_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // Get text from input
        const char *txt = lv_textarea_get_text(s_search_input);
        if (txt) {
            strncpy(s_search_text, txt, sizeof(s_search_text) - 1);
            s_search_text[sizeof(s_search_text) - 1] = '\0';
        }
        
        hide_keyboard();
        apply_filters();
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        // Live update as user types
        const char *txt = lv_textarea_get_text(s_search_input);
        if (txt) {
            strncpy(s_search_text, txt, sizeof(s_search_text) - 1);
            s_search_text[sizeof(s_search_text) - 1] = '\0';
            apply_filters();
        }
    }
}

static void bpm_slider_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t range = lv_slider_get_value(s_bpm_slider);
        
        if (s_reference_bpm > 0) {
            // Range around reference BPM
            s_bpm_min = s_reference_bpm - (float)range;
            s_bpm_max = s_reference_bpm + (float)range;
        } else {
            // Absolute range (slider value is max BPM, min is always 0)
            s_bpm_min = 0;
            s_bpm_max = (float)range;
        }
        
        update_bpm_label();
        apply_filters();
    }
}

static void key_filter_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        // Toggle key filter
        if (s_reference_key < 24) {
            s_key_filter_enabled = !s_key_filter_enabled;
            update_key_filter_label();
            apply_filters();
        }
    }
}

static void result_item_clicked(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    uintptr_t index = (uintptr_t)lv_obj_get_user_data(btn);
    
    // Update selection
    update_selection((int)index);
    
    // Notify callback
    if (index < s_result_count && s_track_cb) {
        s_track_cb(s_results[index].db_index, s_track_cb_data);
    }
}

// ============================================================================
// Keyboard Management
// ============================================================================

static void show_keyboard(void) {
    if (s_keyboard_visible) return;
    
    if (s_keyboard) {
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, s_search_input);
        s_keyboard_visible = true;
        
        // Resize results list to make room for keyboard
        if (s_results_list) {
            lv_obj_set_height(s_results_list, LV_PCT(35));
        }
    }
    
    ESP_LOGD(TAG, "Keyboard shown");
}

static void hide_keyboard(void) {
    if (!s_keyboard_visible) return;
    
    if (s_keyboard) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, NULL);
        s_keyboard_visible = false;
        
        // Restore results list size
        if (s_results_list) {
            lv_obj_set_height(s_results_list, LV_PCT(55));
        }
    }
    
    ESP_LOGD(TAG, "Keyboard hidden");
}

// ============================================================================
// Label Updates
// ============================================================================

static void update_bpm_label(void) {
    if (!s_bpm_label) return;
    
    char buf[32];
    if (s_reference_bpm > 0) {
        snprintf(buf, sizeof(buf), "BPM: %.0f-%.0f", s_bpm_min, s_bpm_max);
    } else {
        int32_t range = lv_slider_get_value(s_bpm_slider);
        if (range >= 200) {
            snprintf(buf, sizeof(buf), "BPM: All");
        } else {
            snprintf(buf, sizeof(buf), "BPM: 0-%ld", (long)range);
        }
    }
    lv_label_set_text(s_bpm_label, buf);
}

static void update_key_filter_label(void) {
    if (!s_key_filter_label) return;
    
    char buf[64];
    if (s_reference_key >= 24) {
        snprintf(buf, sizeof(buf), "Key: All");
    } else if (s_key_filter_enabled) {
        char compat[48];
        get_compatible_keys_str(s_reference_key, compat, sizeof(compat));
        snprintf(buf, sizeof(buf), "Key: %s", compat);
    } else {
        snprintf(buf, sizeof(buf), "Key: All (ref: %s)", KEY_NAMES[s_reference_key]);
    }
    lv_label_set_text(s_key_filter_label, buf);
}

// ============================================================================
// Public API
// ============================================================================

void search_view_init(uint32_t width, uint32_t height) {
    init_styles();
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    
    // Main container
    s_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_container, width, height);
    lv_obj_set_pos(s_container, 0, 0);
    lv_obj_set_style_bg_color(s_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_set_style_pad_all(s_container, 4, 0);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Title
    s_title_label = lv_label_create(s_container);
    lv_obj_set_style_text_color(s_title_label, fg_color, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_title_label, "SEARCH LIBRARY");
    
    // Search input
    s_search_input = lv_textarea_create(s_container);
    lv_obj_set_size(s_search_input, LV_PCT(95), 35);
    lv_textarea_set_placeholder_text(s_search_input, "Search title or artist...");
    lv_textarea_set_one_line(s_search_input, true);
    lv_obj_set_style_bg_color(s_search_input, lv_color_make(32, 32, 32), 0);
    lv_obj_set_style_text_color(s_search_input, fg_color, 0);
    lv_obj_set_style_border_color(s_search_input, fg_color, 0);
    lv_obj_set_style_border_width(s_search_input, 1, 0);
    lv_obj_add_event_cb(s_search_input, search_input_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_search_input, search_input_event_cb, LV_EVENT_FOCUSED, NULL);
    
    // Filter panel (horizontal layout)
    s_filter_panel = lv_obj_create(s_container);
    lv_obj_set_size(s_filter_panel, LV_PCT(95), 45);
    lv_obj_set_style_bg_opa(s_filter_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_filter_panel, 0, 0);
    lv_obj_set_style_pad_all(s_filter_panel, 0, 0);
    lv_obj_set_flex_flow(s_filter_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_filter_panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_filter_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    // BPM filter section
    lv_obj_t *bpm_section = lv_obj_create(s_filter_panel);
    lv_obj_set_size(bpm_section, LV_PCT(48), LV_PCT(100));
    lv_obj_set_style_bg_opa(bpm_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bpm_section, 0, 0);
    lv_obj_set_style_pad_all(bpm_section, 2, 0);
    lv_obj_clear_flag(bpm_section, LV_OBJ_FLAG_SCROLLABLE);
    
    s_bpm_label = lv_label_create(bpm_section);
    lv_obj_set_style_text_color(s_bpm_label, fg_color, 0);
    lv_obj_set_style_text_font(s_bpm_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_bpm_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(s_bpm_label, "BPM: All");
    
    s_bpm_slider = lv_slider_create(bpm_section);
    lv_obj_set_size(s_bpm_slider, LV_PCT(95), 10);
    lv_obj_align(s_bpm_slider, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(s_bpm_slider, 5, 200);  // BPM range
    lv_slider_set_value(s_bpm_slider, 200, LV_ANIM_OFF);  // Default: all
    lv_obj_set_style_bg_color(s_bpm_slider, lv_color_make(64, 64, 64), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bpm_slider, fg_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bpm_slider, fg_color, LV_PART_KNOB);
    lv_obj_add_event_cb(s_bpm_slider, bpm_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Key filter section
    lv_obj_t *key_section = lv_obj_create(s_filter_panel);
    lv_obj_set_size(key_section, LV_PCT(48), LV_PCT(100));
    lv_obj_set_style_bg_opa(key_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(key_section, 0, 0);
    lv_obj_set_style_pad_all(key_section, 2, 0);
    lv_obj_clear_flag(key_section, LV_OBJ_FLAG_SCROLLABLE);
    
    s_key_filter_btn = lv_btn_create(key_section);
    lv_obj_set_size(s_key_filter_btn, LV_PCT(100), LV_PCT(90));
    lv_obj_center(s_key_filter_btn);
    lv_obj_add_style(s_key_filter_btn, &s_style_filter, 0);
    lv_obj_add_event_cb(s_key_filter_btn, key_filter_event_cb, LV_EVENT_CLICKED, NULL);
    
    s_key_filter_label = lv_label_create(s_key_filter_btn);
    lv_obj_set_style_text_color(s_key_filter_label, fg_color, 0);
    lv_obj_set_style_text_font(s_key_filter_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_key_filter_label);
    lv_label_set_text(s_key_filter_label, "Key: All");
    
    // Status label
    s_status_label = lv_label_create(s_container);
    lv_obj_set_style_text_color(s_status_label, lv_color_make(128, 128, 128), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_status_label, "0 tracks found");
    
    // Results list
    s_results_list = lv_list_create(s_container);
    lv_obj_set_size(s_results_list, LV_PCT(100), LV_PCT(55));
    lv_obj_set_style_bg_color(s_results_list, lv_color_black(), 0);
    lv_obj_set_style_border_color(s_results_list, fg_color, 0);
    lv_obj_set_style_border_width(s_results_list, 1, 0);
    lv_obj_set_style_pad_all(s_results_list, 2, 0);
    
    // Keyboard (hidden by default)
    s_keyboard = lv_keyboard_create(s_container);
    lv_obj_set_size(s_keyboard, LV_PCT(100), LV_PCT(40));
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    s_keyboard_visible = false;
    
    // Initially hidden
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
    
    ESP_LOGI(TAG, "Search view initialized: %lux%lu", (unsigned long)width, (unsigned long)height);
}

void search_view_show(void) {
    if (!s_container) return;
    
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = true;
    
    // Apply current filters
    apply_filters();
    
    ESP_LOGI(TAG, "Search view shown");
}

void search_view_hide(void) {
    if (!s_container) return;
    
    hide_keyboard();
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
    
    ESP_LOGI(TAG, "Search view hidden");
}

bool search_view_is_visible(void) {
    return s_visible;
}

void search_view_set_track_callback(search_track_selected_cb cb, void *user_data) {
    s_track_cb = cb;
    s_track_cb_data = user_data;
}

void search_view_set_back_callback(search_back_cb cb, void *user_data) {
    s_back_cb = cb;
    s_back_cb_data = user_data;
}

void search_view_set_reference_key(uint8_t key_id) {
    s_reference_key = key_id;
    s_key_filter_enabled = (key_id < 24);
    update_key_filter_label();
    
    if (s_visible) {
        apply_filters();
    }
    
    ESP_LOGI(TAG, "Reference key set: %s", 
             key_id < 24 ? KEY_NAMES[key_id] : "none");
}

void search_view_set_reference_bpm(float bpm) {
    s_reference_bpm = bpm;
    
    if (bpm > 0) {
        // Set slider to reasonable range
        lv_slider_set_value(s_bpm_slider, (int32_t)BPM_RANGE_DEFAULT, LV_ANIM_OFF);
        s_bpm_min = bpm - BPM_RANGE_DEFAULT;
        s_bpm_max = bpm + BPM_RANGE_DEFAULT;
    } else {
        // Reset to all
        lv_slider_set_value(s_bpm_slider, 200, LV_ANIM_OFF);
        s_bpm_min = 0;
        s_bpm_max = 999;
    }
    
    update_bpm_label();
    
    if (s_visible) {
        apply_filters();
    }
    
    ESP_LOGI(TAG, "Reference BPM set: %.1f", bpm);
}

void search_view_scroll_up(void) {
    if (!s_visible || s_keyboard_visible) return;
    
    if (s_list_item_count > 0 && s_selected_index > 0) {
        update_selection(s_selected_index - 1);
    }
}

void search_view_scroll_down(void) {
    if (!s_visible || s_keyboard_visible) return;
    
    if (s_list_item_count > 0 && s_selected_index < (int)s_list_item_count - 1) {
        update_selection(s_selected_index + 1);
    }
}

void search_view_select(void) {
    if (!s_visible) return;
    
    if (s_keyboard_visible) {
        // Confirm keyboard input
        hide_keyboard();
        apply_filters();
    } else if (s_selected_index >= 0 && s_selected_index < (int)s_result_count) {
        // Select track
        if (s_track_cb) {
            s_track_cb(s_results[s_selected_index].db_index, s_track_cb_data);
        }
    }
}

void search_view_back(void) {
    if (!s_visible) return;
    
    if (s_keyboard_visible) {
        hide_keyboard();
    } else if (s_back_cb) {
        s_back_cb(s_back_cb_data);
    }
}

void search_view_clear_filters(void) {
    // Clear text search
    s_search_text[0] = '\0';
    if (s_search_input) {
        lv_textarea_set_text(s_search_input, "");
    }
    
    // Reset BPM filter
    s_reference_bpm = 0;
    s_bpm_min = 0;
    s_bpm_max = 999;
    if (s_bpm_slider) {
        lv_slider_set_value(s_bpm_slider, 200, LV_ANIM_OFF);
    }
    update_bpm_label();
    
    // Reset key filter
    s_reference_key = 255;
    s_key_filter_enabled = false;
    update_key_filter_label();
    
    if (s_visible) {
        apply_filters();
    }
    
    ESP_LOGI(TAG, "Filters cleared");
}

void search_view_cleanup(void) {
    free_list_items();
    s_result_count = 0;
    s_search_text[0] = '\0';
    
    ESP_LOGI(TAG, "Search view cleanup");
}
