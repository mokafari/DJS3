#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MAX_PLAYLIST_TRACKS 200
#define MAX_TRACK_PATH_LEN 256
#define MAX_TRACK_TITLE_LEN 128

typedef struct {
    char path[MAX_TRACK_PATH_LEN];      // Track file path
    char title[MAX_TRACK_TITLE_LEN];    // From #EXTINF or filename
    uint32_t duration_seconds;           // From #EXTINF or 0
} m3u_entry_t;

typedef struct {
    char name[64];                       // Playlist name (from filename)
    char path[MAX_TRACK_PATH_LEN];       // Full path to .m3u file
    m3u_entry_t *entries;                // Array of entries
    uint32_t entry_count;                // Number of entries
    uint32_t capacity;                   // Allocated capacity
} m3u_playlist_t;

/**
 * @brief Parse an M3U file
 * 
 * @param m3u_path Path to the .m3u or .m3u8 file
 * @return Allocated playlist (caller must free with m3u_playlist_free)
 *         Returns NULL on error
 */
m3u_playlist_t* m3u_parse_file(const char *m3u_path);

/**
 * @brief Free a playlist and all its entries
 * 
 * @param playlist Playlist to free (safe to call with NULL)
 */
void m3u_playlist_free(m3u_playlist_t *playlist);

/**
 * @brief Parse a single line for streaming parsing
 * 
 * @param line The line to parse (without trailing newline)
 * @param base_dir Base directory for resolving relative paths
 * @param entry Output entry (filled if line is a track path)
 * @return true if line was a track entry, false otherwise
 */
bool m3u_parse_line(const char *line, const char *base_dir, m3u_entry_t *entry);

/**
 * @brief Check if a file is an M3U playlist
 * 
 * @param path File path to check
 * @return true if file has .m3u or .m3u8 extension
 */
bool m3u_is_playlist_file(const char *path);
