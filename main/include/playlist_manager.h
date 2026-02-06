#pragma once
#include "m3u_parser.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_PLAYLISTS 50
#define PLAYLIST_DIR "/sdcard/Playlists"

// Playlist info for browsing (lightweight - doesn't load all tracks)
typedef struct {
    char name[64];
    char path[MAX_TRACK_PATH_LEN];
    uint32_t track_count;
    bool is_loaded;  // true if full playlist is in memory
} playlist_info_t;

// Initialize playlist manager
// Scans PLAYLIST_DIR for .m3u files
bool playlist_manager_init(void);

// Rescan for playlists
void playlist_manager_rescan(void);

// Get number of available playlists
uint32_t playlist_manager_get_count(void);

// Get playlist info by index (for browsing)
bool playlist_manager_get_info(uint32_t index, playlist_info_t *info);

// Load a full playlist into memory
// Returns loaded playlist (managed by manager - do NOT free)
m3u_playlist_t* playlist_manager_load(uint32_t index);

// Get currently loaded playlist (or NULL)
m3u_playlist_t* playlist_manager_get_current(void);

// Unload current playlist (free memory)
void playlist_manager_unload(void);

// Create new playlist
// Returns index of new playlist, or -1 on error
int playlist_manager_create(const char *name);

// Delete playlist by index
bool playlist_manager_delete(uint32_t index);

// Save current playlist (after modifications)
bool playlist_manager_save_current(void);

// Add track to current playlist
bool playlist_manager_add_track(const char *path, const char *title, uint32_t duration);

// Get track from current playlist
bool playlist_manager_get_track(uint32_t index, m3u_entry_t *entry);
