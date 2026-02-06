#pragma once

#include "m3u_parser.h"
#include <stdbool.h>

/**
 * @brief Create a new empty playlist
 * 
 * @param name Playlist name
 * @return Allocated playlist (caller must free with m3u_playlist_free)
 */
m3u_playlist_t* m3u_playlist_create(const char *name);

/**
 * @brief Add a track to playlist
 * 
 * @param playlist Playlist to add to
 * @param path Full path to audio file
 * @param title Display title (optional, can be NULL - will use filename)
 * @param duration Duration in seconds (0 if unknown)
 * @return true on success, false if playlist is full or invalid
 */
bool m3u_playlist_add_track(m3u_playlist_t *playlist, const char *path,
                            const char *title, uint32_t duration);

/**
 * @brief Remove track by index
 * 
 * @param playlist Playlist to modify
 * @param index Index of track to remove
 * @return true on success, false if index out of bounds
 */
bool m3u_playlist_remove_track(m3u_playlist_t *playlist, uint32_t index);

/**
 * @brief Move track (reorder)
 * 
 * @param playlist Playlist to modify
 * @param from_index Current position of track
 * @param to_index New position for track
 * @return true on success, false if indices out of bounds
 */
bool m3u_playlist_move_track(m3u_playlist_t *playlist, uint32_t from_index, uint32_t to_index);

/**
 * @brief Save playlist to file (extended M3U format)
 * 
 * If playlist->path is set, saves there; otherwise uses name to create path.
 * Uses atomic write (temp file + rename) for safety.
 * 
 * @param playlist Playlist to save
 * @param base_dir Directory to save in (e.g., "/sdcard/Playlists")
 * @return true on success, false on error
 */
bool m3u_playlist_save(m3u_playlist_t *playlist, const char *base_dir);

/**
 * @brief Save with extended M3U format (#EXTINF tags)
 * 
 * @param playlist Playlist to save
 * @param base_dir Directory to save in
 * @return true on success, false on error
 */
bool m3u_playlist_save_extended(m3u_playlist_t *playlist, const char *base_dir);
