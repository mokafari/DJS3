/**
 * @file settings_view.c
 * @brief Settings menu UI view with categories and sub-menus
 * 
 * Provides a hierarchical settings menu with:
 * - Theme settings (color scheme, brightness)
 * - Audio settings (output mode, volume)
 * - Display settings (waveform resolution, screen timeout)
 */

#include "settings_view.h"
#include "hud_theme.h"
#include "ui_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "settings_view";

// ============================================================================
// Settings Definitions
// ============================================================================

#define MAX_MENU_ITEMS 8

// Setting IDs for each category
typedef enum {
    // Theme settings
    SETTING_THEME_COLOR = 0,
    SETTING_THEME_BRIGHTNESS,
    SETTING_THEME_COUNT,
    
    // Audio settings
    SETTING_AUDIO_MODE = 0,
    SETTING_AUDIO_VOLUME,
    SETTING_AUDIO_COUNT,
    
    // Display settings
    SETTING_DISPLAY_RESOLUTION = 0,
    SETTING_DISPLAY_TIMEOUT,
    SETTING_DISPLAY_COUNT
} setting_id_t;

// Theme color names
static const char *theme_names[] = {"Amber", "Cyan", "Green"};
#define THEME_COUNT 3

// Audio mode names
static const char *audio_mode_names[] = {"Simple", "Granular"};
#define AUDIO_MODE_COUNT 2

// Waveform resolution options
static const int resolution_values[] = {1, 2, 4, 8};
static const char *resolution_names[] = {"480 bars", "240 bars", "120 bars", "60 bars"};
#define RESOLUTION_COUNT 4

// Screen timeout options (seconds)
static const int timeout_values[] = {0, 30, 60, 120, 300};
static const char *timeout_names[] = {"Off", "30s", "1 min", "2 min", "5 min"};
#define TIMEOUT_COUNT 5

// ============================================================================
// Current Settings State
// ============================================================================

typedef struct {
    int theme_index;           // 0-2 (Amber, Cyan, Green)
    int brightness;            // 0-255
    int audio_mode;            // 0=Simple, 1=Granular
    int volume;                // 0-100
    int resolution_index;      // Index into resolution_values
    int timeout_index;         // Index into timeout_values
} settings_state_t;

static settings_state_t s_settings = {
    .theme_index = 0,          // Amber default
    .brightness = 200,         // Default brightness
    .audio_mode = 0,           // Simple mode
    .volume = 80,              // 80% volume
    .resolution_index = 0,     // Full resolution
    .timeout_index = 0         // Timeout disabled
};

// ============================================================================
// UI State
// ============================================================================

typedef enum {
    MENU_LEVEL_CATEGORIES = 0,  // Top level (Theme, Audio, Display)
    MENU_LEVEL_SETTINGS         // Setting items within a category
} menu_level_t;

static lv_obj_t *s_container = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_title_label = NULL;
static bool s_visible = false;

static menu_level_t s_menu_level = MENU_LEVEL_CATEGORIES;
static settings_category_t s_current_category = SETTINGS_CAT_THEME;
static int s_selected_index = 0;

// List item management
static lv_obj_t *s_list_items[MAX_MENU_ITEMS];
static size_t s_item_count = 0;

// Callbacks
static settings_changed_cb s_changed_cb = NULL;
static void *s_changed_cb_data = NULL;
static settings_back_cb s_back_cb = NULL;
static void *s_back_cb_data = NULL;

// Styles
static lv_style_t s_style_normal;
static lv_style_t s_style_selected;
static lv_style_t s_style_value;
static bool s_styles_initialized = false;

// ============================================================================
// Forward Declarations
// ============================================================================

static void init_styles(void);
static void show_categories(void);
static void show_settings_for_category(settings_category_t category);
static void update_selection(int new_index);
static void clear_list(void);
static void apply_setting_change(settings_category_t category, int setting_id, int delta);
static void list_item_clicked(lv_event_t *e);
static const char* get_current_value_string(settings_category_t category, int setting_id);

// ============================================================================
// Style Initialization
// ============================================================================

static void init_styles(void) {
    if (s_styles_initialized) return;
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    
    // Normal style (foreground color on black)
    lv_style_init(&s_style_normal);
    lv_style_set_text_color(&s_style_normal, fg_color);
    lv_style_set_bg_color(&s_style_normal, lv_color_black());
    lv_style_set_bg_opa(&s_style_normal, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_style_normal, 0);
    lv_style_set_pad_all(&s_style_normal, 4);
    
    // Selected style (black text on foreground color background) - inverted
    lv_style_init(&s_style_selected);
    lv_style_set_text_color(&s_style_selected, lv_color_black());
    lv_style_set_bg_color(&s_style_selected, fg_color);
    lv_style_set_bg_opa(&s_style_selected, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_selected, 0);
    lv_style_set_pad_all(&s_style_selected, 4);
    
    // Value style (dimmer foreground for current values)
    lv_style_init(&s_style_value);
    lv_style_set_text_color(&s_style_value, lv_color_make(128, 128, 128));
    lv_style_set_text_font(&s_style_value, &lv_font_montserrat_14);
    
    s_styles_initialized = true;
}

// ============================================================================
// List Management
// ============================================================================

static void clear_list(void) {
    if (s_list) {
        lv_obj_clean(s_list);
    }
    memset(s_list_items, 0, sizeof(s_list_items));
    s_item_count = 0;
    s_selected_index = 0;
}

static void update_selection(int new_index) {
    if (new_index < 0 || new_index >= (int)s_item_count) {
        return;
    }
    
    // Remove selection from current item
    if (s_selected_index >= 0 && s_selected_index < (int)s_item_count && s_list_items[s_selected_index]) {
        lv_obj_remove_style(s_list_items[s_selected_index], &s_style_selected, 0);
        lv_obj_add_style(s_list_items[s_selected_index], &s_style_normal, 0);
    }
    
    // Apply selection to new item
    s_selected_index = new_index;
    if (s_list_items[s_selected_index]) {
        lv_obj_remove_style(s_list_items[s_selected_index], &s_style_normal, 0);
        lv_obj_add_style(s_list_items[s_selected_index], &s_style_selected, 0);
        
        // Scroll to selected item
        lv_obj_scroll_to_view(s_list_items[s_selected_index], LV_ANIM_ON);
    }
}

static lv_obj_t* add_menu_item(const char *icon, const char *text, int user_data) {
    if (s_item_count >= MAX_MENU_ITEMS) {
        return NULL;
    }
    
    lv_obj_t *btn = lv_list_add_btn(s_list, icon, text);
    lv_obj_add_style(btn, &s_style_normal, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
    lv_obj_set_user_data(btn, (void *)(intptr_t)user_data);
    lv_obj_add_event_cb(btn, list_item_clicked, LV_EVENT_CLICKED, NULL);
    
    s_list_items[s_item_count] = btn;
    s_item_count++;
    
    return btn;
}

// ============================================================================
// Value String Helpers
// ============================================================================

static const char* get_current_value_string(settings_category_t category, int setting_id) {
    static char buffer[32];
    
    switch (category) {
        case SETTINGS_CAT_THEME:
            if (setting_id == SETTING_THEME_COLOR) {
                return theme_names[s_settings.theme_index];
            } else if (setting_id == SETTING_THEME_BRIGHTNESS) {
                snprintf(buffer, sizeof(buffer), "%d%%", (s_settings.brightness * 100) / 255);
                return buffer;
            }
            break;
            
        case SETTINGS_CAT_AUDIO:
            if (setting_id == SETTING_AUDIO_MODE) {
                return audio_mode_names[s_settings.audio_mode];
            } else if (setting_id == SETTING_AUDIO_VOLUME) {
                snprintf(buffer, sizeof(buffer), "%d%%", s_settings.volume);
                return buffer;
            }
            break;
            
        case SETTINGS_CAT_DISPLAY:
            if (setting_id == SETTING_DISPLAY_RESOLUTION) {
                return resolution_names[s_settings.resolution_index];
            } else if (setting_id == SETTING_DISPLAY_TIMEOUT) {
                return timeout_names[s_settings.timeout_index];
            }
            break;
            
        default:
            break;
    }
    
    return "?";
}

// ============================================================================
// Menu Display Functions
// ============================================================================

static void show_categories(void) {
    clear_list();
    
    lv_label_set_text(s_title_label, "SETTINGS");
    s_menu_level = MENU_LEVEL_CATEGORIES;
    
    // Add category items
    add_menu_item(LV_SYMBOL_TINT, "Theme", SETTINGS_CAT_THEME);
    add_menu_item(LV_SYMBOL_AUDIO, "Audio", SETTINGS_CAT_AUDIO);
    add_menu_item(LV_SYMBOL_IMAGE, "Display", SETTINGS_CAT_DISPLAY);
    
    // Select first item
    if (s_item_count > 0) {
        update_selection(0);
    }
    
    ESP_LOGI(TAG, "Showing settings categories");
}

static void show_settings_for_category(settings_category_t category) {
    clear_list();
    
    s_menu_level = MENU_LEVEL_SETTINGS;
    s_current_category = category;
    
    char label_buf[64];
    
    switch (category) {
        case SETTINGS_CAT_THEME:
            lv_label_set_text(s_title_label, "THEME");
            
            // Color scheme
            snprintf(label_buf, sizeof(label_buf), "Color: %s", 
                     get_current_value_string(category, SETTING_THEME_COLOR));
            add_menu_item(LV_SYMBOL_TINT, label_buf, SETTING_THEME_COLOR);
            
            // Brightness
            snprintf(label_buf, sizeof(label_buf), "Brightness: %s", 
                     get_current_value_string(category, SETTING_THEME_BRIGHTNESS));
            add_menu_item(LV_SYMBOL_EYE_OPEN, label_buf, SETTING_THEME_BRIGHTNESS);
            break;
            
        case SETTINGS_CAT_AUDIO:
            lv_label_set_text(s_title_label, "AUDIO");
            
            // Audio mode
            snprintf(label_buf, sizeof(label_buf), "Mode: %s", 
                     get_current_value_string(category, SETTING_AUDIO_MODE));
            add_menu_item(LV_SYMBOL_SETTINGS, label_buf, SETTING_AUDIO_MODE);
            
            // Volume
            snprintf(label_buf, sizeof(label_buf), "Volume: %s", 
                     get_current_value_string(category, SETTING_AUDIO_VOLUME));
            add_menu_item(LV_SYMBOL_VOLUME_MAX, label_buf, SETTING_AUDIO_VOLUME);
            break;
            
        case SETTINGS_CAT_DISPLAY:
            lv_label_set_text(s_title_label, "DISPLAY");
            
            // Waveform resolution
            snprintf(label_buf, sizeof(label_buf), "Waveform: %s", 
                     get_current_value_string(category, SETTING_DISPLAY_RESOLUTION));
            add_menu_item(LV_SYMBOL_IMAGE, label_buf, SETTING_DISPLAY_RESOLUTION);
            
            // Screen timeout
            snprintf(label_buf, sizeof(label_buf), "Timeout: %s", 
                     get_current_value_string(category, SETTING_DISPLAY_TIMEOUT));
            add_menu_item(LV_SYMBOL_POWER, label_buf, SETTING_DISPLAY_TIMEOUT);
            break;
            
        default:
            return;
    }
    
    // Add back item
    add_menu_item(LV_SYMBOL_LEFT, "Back", -1);
    
    // Select first item
    if (s_item_count > 0) {
        update_selection(0);
    }
    
    ESP_LOGI(TAG, "Showing settings for category %d", category);
}

// ============================================================================
// Setting Change Logic
// ============================================================================

static void apply_setting_change(settings_category_t category, int setting_id, int delta) {
    int old_value = 0;
    int new_value = 0;
    
    switch (category) {
        case SETTINGS_CAT_THEME:
            if (setting_id == SETTING_THEME_COLOR) {
                old_value = s_settings.theme_index;
                new_value = (s_settings.theme_index + delta + THEME_COUNT) % THEME_COUNT;
                s_settings.theme_index = new_value;
                
                // Apply theme change immediately
                ui_manager_set_theme((ui_theme_t)new_value);
                
                // Reinitialize styles with new color
                s_styles_initialized = false;
                init_styles();
                
            } else if (setting_id == SETTING_THEME_BRIGHTNESS) {
                old_value = s_settings.brightness;
                new_value = s_settings.brightness + (delta * 25);  // Step by ~10%
                if (new_value < 0) new_value = 0;
                if (new_value > 255) new_value = 255;
                s_settings.brightness = new_value;
                
                // TODO: Apply brightness to backlight via LEDC
            }
            break;
            
        case SETTINGS_CAT_AUDIO:
            if (setting_id == SETTING_AUDIO_MODE) {
                old_value = s_settings.audio_mode;
                new_value = (s_settings.audio_mode + delta + AUDIO_MODE_COUNT) % AUDIO_MODE_COUNT;
                s_settings.audio_mode = new_value;
                
                // TODO: Apply audio mode change
                
            } else if (setting_id == SETTING_AUDIO_VOLUME) {
                old_value = s_settings.volume;
                new_value = s_settings.volume + (delta * 10);  // Step by 10%
                if (new_value < 0) new_value = 0;
                if (new_value > 100) new_value = 100;
                s_settings.volume = new_value;
                
                // TODO: Apply volume change
            }
            break;
            
        case SETTINGS_CAT_DISPLAY:
            if (setting_id == SETTING_DISPLAY_RESOLUTION) {
                old_value = s_settings.resolution_index;
                new_value = (s_settings.resolution_index + delta + RESOLUTION_COUNT) % RESOLUTION_COUNT;
                s_settings.resolution_index = new_value;
                
                // Apply resolution change
                ui_manager_set_waveform_resolution(resolution_values[new_value]);
                
            } else if (setting_id == SETTING_DISPLAY_TIMEOUT) {
                old_value = s_settings.timeout_index;
                new_value = (s_settings.timeout_index + delta + TIMEOUT_COUNT) % TIMEOUT_COUNT;
                s_settings.timeout_index = new_value;
                
                // TODO: Apply screen timeout change
            }
            break;
            
        default:
            return;
    }
    
    // Notify callback
    if (s_changed_cb) {
        s_changed_cb(category, setting_id, new_value, s_changed_cb_data);
    }
    
    // Refresh the current menu to show new value
    show_settings_for_category(category);
    
    // Restore selection position
    if (s_selected_index < (int)s_item_count) {
        update_selection(s_selected_index);
    }
    
    ESP_LOGI(TAG, "Setting changed: cat=%d, id=%d, old=%d, new=%d", 
             category, setting_id, old_value, new_value);
}

// ============================================================================
// Event Handlers
// ============================================================================

static void list_item_clicked(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    intptr_t user_data = (intptr_t)lv_obj_get_user_data(btn);
    
    // Find and update selection to clicked item
    for (size_t i = 0; i < s_item_count; i++) {
        if (s_list_items[i] == btn) {
            update_selection((int)i);
            break;
        }
    }
    
    if (s_menu_level == MENU_LEVEL_CATEGORIES) {
        // Enter category
        if (user_data >= 0 && user_data < SETTINGS_CAT_COUNT) {
            show_settings_for_category((settings_category_t)user_data);
        }
    } else {
        // In settings menu
        if (user_data == -1) {
            // Back button
            show_categories();
        } else {
            // Toggle/cycle the setting value
            apply_setting_change(s_current_category, (int)user_data, 1);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void settings_view_init(uint32_t width, uint32_t height) {
    init_styles();
    
    // Create container (full screen)
    s_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_container, width, height);
    lv_obj_set_pos(s_container, 0, 0);
    lv_obj_set_style_bg_color(s_container, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_container, 0, 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title label
    s_title_label = lv_label_create(s_container);
    lv_color_t fg_color = hud_theme_get_foreground_color();
    lv_obj_set_style_text_color(s_title_label, fg_color, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_label_set_text(s_title_label, "SETTINGS");
    
    // List container
    s_list = lv_list_create(s_container);
    lv_obj_set_size(s_list, width, height - 30);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    
    // Initially hidden
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
    
    ESP_LOGI(TAG, "Settings view initialized: %lux%lu", (unsigned long)width, (unsigned long)height);
}

void settings_view_show(void) {
    if (!s_container) return;
    
    // Always start at category level
    show_categories();
    
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = true;
    
    ESP_LOGI(TAG, "Settings view shown");
}

void settings_view_hide(void) {
    if (!s_container) return;
    
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
    
    ESP_LOGI(TAG, "Settings view hidden");
}

bool settings_view_is_visible(void) {
    return s_visible;
}

void settings_view_refresh(void) {
    if (!s_visible) return;
    
    if (s_menu_level == MENU_LEVEL_CATEGORIES) {
        show_categories();
    } else {
        show_settings_for_category(s_current_category);
    }
}

void settings_view_set_changed_callback(settings_changed_cb cb, void *user_data) {
    s_changed_cb = cb;
    s_changed_cb_data = user_data;
}

void settings_view_set_back_callback(settings_back_cb cb, void *user_data) {
    s_back_cb = cb;
    s_back_cb_data = user_data;
}

// ============================================================================
// Navigation
// ============================================================================

void settings_view_scroll_up(void) {
    if (!s_visible || s_item_count == 0) return;
    
    int new_index = s_selected_index - 1;
    if (new_index >= 0) {
        update_selection(new_index);
    }
}

void settings_view_scroll_down(void) {
    if (!s_visible || s_item_count == 0) return;
    
    int new_index = s_selected_index + 1;
    if (new_index < (int)s_item_count) {
        update_selection(new_index);
    }
}

void settings_view_select(void) {
    if (!s_visible || s_selected_index < 0 || s_selected_index >= (int)s_item_count) {
        return;
    }
    
    // Simulate click on selected item
    if (s_list_items[s_selected_index]) {
        lv_event_send(s_list_items[s_selected_index], LV_EVENT_CLICKED, NULL);
    }
}

void settings_view_back(void) {
    if (!s_visible) return;
    
    if (s_menu_level == MENU_LEVEL_SETTINGS) {
        // Go back to categories
        show_categories();
    } else {
        // Exit settings view
        if (s_back_cb) {
            s_back_cb(s_back_cb_data);
        }
    }
}

void settings_view_adjust_value(int delta) {
    if (!s_visible || s_menu_level != MENU_LEVEL_SETTINGS) return;
    if (s_selected_index < 0 || s_selected_index >= (int)s_item_count) return;
    
    intptr_t setting_id = (intptr_t)lv_obj_get_user_data(s_list_items[s_selected_index]);
    
    // Don't adjust the back button
    if (setting_id < 0) return;
    
    apply_setting_change(s_current_category, (int)setting_id, delta);
}

// ============================================================================
// Settings Getters
// ============================================================================

int settings_get_theme(void) {
    return s_settings.theme_index;
}

int settings_get_brightness(void) {
    return s_settings.brightness;
}

int settings_get_volume(void) {
    return s_settings.volume;
}

int settings_get_audio_mode(void) {
    return s_settings.audio_mode;
}

int settings_get_waveform_resolution(void) {
    return resolution_values[s_settings.resolution_index];
}

int settings_get_screen_timeout(void) {
    return timeout_values[s_settings.timeout_index];
}
