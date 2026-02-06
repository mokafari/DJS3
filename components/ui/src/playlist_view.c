/**
 * @file playlist_view.c
 * @brief Playlist browser UI view for selecting playlists and tracks
 */

#include "playlist_view.h"
#include "playlist_manager.h"
#include "m3u_parser.h"
#include "hud_theme.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "playlist_view";

// UI state
static lv_obj_t *s_container = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_title_label = NULL;
static bool s_visible = false;
static bool s_showing_tracks = false;  // false = playlist list, true = track list
static int s_selected_playlist = -1;
static int s_selected_index = -1;      // Currently selected list item

// Callbacks
static playlist_track_selected_cb s_track_cb = NULL;
static void *s_track_cb_data = NULL;
static playlist_back_cb s_back_cb = NULL;
static void *s_back_cb_data = NULL;

// Item management
static lv_obj_t **s_list_items = NULL;
static size_t s_item_count = 0;

// Styles
static lv_style_t s_style_normal;
static lv_style_t s_style_selected;
static bool s_styles_initialized = false;

// Forward declarations
static void show_playlist_list(void);
static void show_track_list(int playlist_index);
static void list_item_clicked(lv_event_t *e);
static void init_styles(void);
static void update_selection(int new_index);
static void free_list_items(void);

static void init_styles(void) {
    if (s_styles_initialized) return;
    
    // Get foreground color from theme
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
    
    s_styles_initialized = true;
}

static void free_list_items(void) {
    if (s_list_items) {
        free(s_list_items);
        s_list_items = NULL;
    }
    s_item_count = 0;
    s_selected_index = -1;
}

static void update_selection(int new_index) {
    if (!s_list_items || new_index < 0 || new_index >= (int)s_item_count) {
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

void playlist_view_init(void) {
    init_styles();
    
    // Create container (full screen)
    s_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(100));
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
    lv_label_set_text(s_title_label, "PLAYLISTS");
    
    // List container
    s_list = lv_list_create(s_container);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100) - 30);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    
    // Initially hidden
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
    
    ESP_LOGI(TAG, "Playlist view initialized");
}

void playlist_view_show(void) {
    if (!s_container) return;
    
    show_playlist_list();
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = true;
}

void playlist_view_hide(void) {
    if (!s_container) return;
    
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
}

bool playlist_view_is_visible(void) {
    return s_visible;
}

void playlist_view_refresh(void) {
    if (s_showing_tracks && s_selected_playlist >= 0) {
        show_track_list(s_selected_playlist);
    } else {
        show_playlist_list();
    }
}

void playlist_view_set_track_callback(playlist_track_selected_cb cb, void *user_data) {
    s_track_cb = cb;
    s_track_cb_data = user_data;
}

void playlist_view_set_back_callback(playlist_back_cb cb, void *user_data) {
    s_back_cb = cb;
    s_back_cb_data = user_data;
}

static void show_playlist_list(void) {
    if (!s_list) return;
    
    // Clear existing items
    free_list_items();
    lv_obj_clean(s_list);
    
    lv_label_set_text(s_title_label, "PLAYLISTS");
    s_showing_tracks = false;
    s_selected_playlist = -1;
    
    // Ensure playlist manager is initialized
    playlist_manager_init();
    
    uint32_t count = playlist_manager_get_count();
    
    if (count == 0) {
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, "No playlists found");
        lv_obj_add_style(btn, &s_style_normal, 0);
        lv_obj_set_style_text_color(btn, lv_color_make(128, 128, 128), 0);
        return;
    }
    
    // Allocate item array
    s_list_items = (lv_obj_t**)calloc(count, sizeof(lv_obj_t*));
    if (!s_list_items) {
        ESP_LOGE(TAG, "Failed to allocate list items array");
        return;
    }
    s_item_count = count;
    
    for (uint32_t i = 0; i < count; i++) {
        playlist_info_t info;
        if (playlist_manager_get_info(i, &info)) {
            char label[80];
            snprintf(label, sizeof(label), "%s (%lu tracks)", info.name, (unsigned long)info.track_count);
            
            lv_obj_t *btn = lv_list_add_btn(s_list, LV_SYMBOL_LIST, label);
            lv_obj_add_style(btn, &s_style_normal, 0);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
            
            // Store index in user data
            lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
            lv_obj_add_event_cb(btn, list_item_clicked, LV_EVENT_CLICKED, NULL);
            
            s_list_items[i] = btn;
        }
    }
    
    // Select first item by default
    if (s_item_count > 0) {
        update_selection(0);
    }
    
    ESP_LOGI(TAG, "Showing %lu playlists", (unsigned long)count);
}

static void show_track_list(int playlist_index) {
    if (!s_list || playlist_index < 0) return;
    
    // Load playlist
    m3u_playlist_t *playlist = playlist_manager_load(playlist_index);
    if (!playlist) {
        ESP_LOGW(TAG, "Failed to load playlist %d", playlist_index);
        return;
    }
    
    // Clear existing items
    free_list_items();
    lv_obj_clean(s_list);
    
    // Update title
    lv_label_set_text(s_title_label, playlist->name);
    s_showing_tracks = true;
    s_selected_playlist = playlist_index;
    
    // Count items: 1 back button + track_count tracks
    uint32_t total_items = 1 + playlist->entry_count;
    
    // Allocate item array
    s_list_items = (lv_obj_t**)calloc(total_items, sizeof(lv_obj_t*));
    if (!s_list_items) {
        ESP_LOGE(TAG, "Failed to allocate list items array");
        return;
    }
    s_item_count = total_items;
    
    // Add back button
    lv_obj_t *back_btn = lv_list_add_btn(s_list, LV_SYMBOL_LEFT, "Back to Playlists");
    lv_obj_add_style(back_btn, &s_style_normal, 0);
    lv_obj_set_style_text_font(back_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_btn, lv_color_make(128, 128, 128), 0);
    lv_obj_set_user_data(back_btn, (void *)(uintptr_t)-1);  // -1 = back
    lv_obj_add_event_cb(back_btn, list_item_clicked, LV_EVENT_CLICKED, NULL);
    s_list_items[0] = back_btn;
    
    // Add tracks
    for (uint32_t i = 0; i < playlist->entry_count; i++) {
        m3u_entry_t *entry = &playlist->entries[i];
        
        lv_obj_t *btn = lv_list_add_btn(s_list, LV_SYMBOL_AUDIO, entry->title);
        lv_obj_add_style(btn, &s_style_normal, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        
        // Store track index (offset by 1 to distinguish from back button)
        lv_obj_set_user_data(btn, (void *)(uintptr_t)(i + 1));
        lv_obj_add_event_cb(btn, list_item_clicked, LV_EVENT_CLICKED, NULL);
        
        s_list_items[i + 1] = btn;
    }
    
    // Select first item (back button)
    if (s_item_count > 0) {
        update_selection(0);
    }
    
    ESP_LOGI(TAG, "Showing %lu tracks from playlist: %s", 
             (unsigned long)playlist->entry_count, playlist->name);
}

static void list_item_clicked(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    intptr_t index = (intptr_t)lv_obj_get_user_data(btn);
    
    // Update visual selection to clicked item
    for (size_t i = 0; i < s_item_count; i++) {
        if (s_list_items[i] == btn) {
            update_selection((int)i);
            break;
        }
    }
    
    if (s_showing_tracks) {
        // Track list mode
        if (index == -1) {
            // Back button
            show_playlist_list();
        } else {
            // Track selected
            m3u_entry_t entry;
            if (playlist_manager_get_track(index - 1, &entry)) {
                ESP_LOGI(TAG, "Track selected: %s", entry.path);
                if (s_track_cb) {
                    s_track_cb(entry.path, s_track_cb_data);
                }
            }
        }
    } else {
        // Playlist list mode - enter playlist
        show_track_list((int)index);
    }
}

void playlist_view_scroll_up(void) {
    if (!s_list || !s_visible || s_item_count == 0) return;
    
    int new_index = s_selected_index - 1;
    if (new_index >= 0) {
        update_selection(new_index);
    }
}

void playlist_view_scroll_down(void) {
    if (!s_list || !s_visible || s_item_count == 0) return;
    
    int new_index = s_selected_index + 1;
    if (new_index < (int)s_item_count) {
        update_selection(new_index);
    }
}

void playlist_view_select(void) {
    if (!s_visible || s_selected_index < 0 || s_selected_index >= (int)s_item_count) {
        return;
    }
    
    // Simulate click on selected item
    if (s_list_items && s_list_items[s_selected_index]) {
        lv_event_send(s_list_items[s_selected_index], LV_EVENT_CLICKED, NULL);
    }
}

void playlist_view_back(void) {
    if (s_showing_tracks) {
        show_playlist_list();
    } else if (s_back_cb) {
        s_back_cb(s_back_cb_data);
    }
}
