#pragma once
#include <stdbool.h>
#include <stdint.h>

// Initialize playlist view (call once during UI init)
void playlist_view_init(void);

// Show/hide playlist view
void playlist_view_show(void);
void playlist_view_hide(void);
bool playlist_view_is_visible(void);

// Refresh playlist list (call after creating/deleting playlists)
void playlist_view_refresh(void);

// Set callback for when a track is selected from playlist
typedef void (*playlist_track_selected_cb)(const char *track_path, void *user_data);
void playlist_view_set_track_callback(playlist_track_selected_cb cb, void *user_data);

// Set callback for when user wants to go back
typedef void (*playlist_back_cb)(void *user_data);
void playlist_view_set_back_callback(playlist_back_cb cb, void *user_data);

// Navigation (for hardware buttons)
void playlist_view_scroll_up(void);
void playlist_view_scroll_down(void);
void playlist_view_select(void);  // Enter playlist or select track
void playlist_view_back(void);    // Go back to playlist list
