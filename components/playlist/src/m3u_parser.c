/**
 * @file m3u_parser.c
 * @brief M3U and M3U8 playlist file parser
 * 
 * Supports:
 * - Standard M3U format
 * - Extended M3U with #EXTINF metadata
 * - Both absolute and relative paths
 * - UTF-8 filenames
 */

#include "m3u_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper function prototypes
static void trim_whitespace(char *str);
static void get_directory(const char *path, char *dir, size_t dir_size);
static void extract_filename(const char *path, char *name, size_t name_size);
static void extract_filename_no_ext(const char *path, char *name, size_t name_size);
static void resolve_path(const char *path, const char *base_dir, char *resolved, size_t resolved_size);
static void parse_extinf(const char *extinf, uint32_t *duration, char *title, size_t title_size);
static void normalize_path_separators(char *path);

/**
 * @brief Trim leading and trailing whitespace from a string in-place
 */
static void trim_whitespace(char *str) {
    if (!str || !*str) return;
    
    // Trim leading whitespace
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    
    // Trim trailing whitespace (including \r\n)
    char *end = start + strlen(start) - 1;
    while (end >= start && (isspace((unsigned char)*end) || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    
    // Shift string if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * @brief Extract the directory portion of a path
 */
static void get_directory(const char *path, char *dir, size_t dir_size) {
    if (!path || !dir || dir_size == 0) return;
    
    strncpy(dir, path, dir_size - 1);
    dir[dir_size - 1] = '\0';
    
    // Find last separator
    char *last_sep = strrchr(dir, '/');
    char *last_sep_win = strrchr(dir, '\\');
    
    if (last_sep_win && (!last_sep || last_sep_win > last_sep)) {
        last_sep = last_sep_win;
    }
    
    if (last_sep) {
        *(last_sep + 1) = '\0';  // Keep trailing separator
    } else {
        dir[0] = '\0';  // No directory
    }
}

/**
 * @brief Extract filename from a path (with extension)
 */
static void extract_filename(const char *path, char *name, size_t name_size) {
    if (!path || !name || name_size == 0) return;
    
    // Find last separator
    const char *last_sep = strrchr(path, '/');
    const char *last_sep_win = strrchr(path, '\\');
    
    if (last_sep_win && (!last_sep || last_sep_win > last_sep)) {
        last_sep = last_sep_win;
    }
    
    const char *filename = last_sep ? last_sep + 1 : path;
    strncpy(name, filename, name_size - 1);
    name[name_size - 1] = '\0';
}

/**
 * @brief Extract filename from a path (without extension)
 */
static void extract_filename_no_ext(const char *path, char *name, size_t name_size) {
    extract_filename(path, name, name_size);
    
    // Remove extension
    char *dot = strrchr(name, '.');
    if (dot && dot != name) {
        *dot = '\0';
    }
}

/**
 * @brief Normalize path separators to forward slashes
 */
static void normalize_path_separators(char *path) {
    if (!path) return;
    
    for (char *p = path; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

/**
 * @brief Resolve a path that may be relative to a base directory
 */
static void resolve_path(const char *path, const char *base_dir, char *resolved, size_t resolved_size) {
    if (!path || !resolved || resolved_size == 0) return;
    
    // Make a mutable copy
    char path_copy[MAX_TRACK_PATH_LEN];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    normalize_path_separators(path_copy);
    
    // Check if path is absolute
    bool is_absolute = (path_copy[0] == '/') || 
                       (strlen(path_copy) > 2 && path_copy[1] == ':');  // Windows drive letter
    
    if (is_absolute || !base_dir || !base_dir[0]) {
        strncpy(resolved, path_copy, resolved_size - 1);
        resolved[resolved_size - 1] = '\0';
        return;
    }
    
    // Combine base_dir and relative path
    char base_copy[MAX_TRACK_PATH_LEN];
    strncpy(base_copy, base_dir, sizeof(base_copy) - 1);
    base_copy[sizeof(base_copy) - 1] = '\0';
    normalize_path_separators(base_copy);
    
    // Ensure base_dir ends with /
    size_t base_len = strlen(base_copy);
    if (base_len > 0 && base_copy[base_len - 1] != '/') {
        if (base_len < sizeof(base_copy) - 1) {
            base_copy[base_len] = '/';
            base_copy[base_len + 1] = '\0';
            base_len++;
        }
    }
    
    // Build combined path
    snprintf(resolved, resolved_size, "%s%s", base_copy, path_copy);
    
    // Resolve .. and . components
    char *src = resolved;
    char *dst = resolved;
    char *components[64];
    int num_components = 0;
    
    // Skip leading /
    if (*src == '/') {
        dst++;
        src++;
    }
    
    // Parse components
    char *token = strtok(src, "/");
    while (token && num_components < 64) {
        if (strcmp(token, ".") == 0) {
            // Skip current directory
        } else if (strcmp(token, "..") == 0) {
            // Go up one level
            if (num_components > 0) {
                num_components--;
            }
        } else {
            components[num_components++] = token;
        }
        token = strtok(NULL, "/");
    }
    
    // Rebuild path
    dst = resolved;
    if (resolved[0] == '/') {
        dst++;
    }
    *dst = '\0';
    
    for (int i = 0; i < num_components; i++) {
        if (i > 0) {
            strcat(dst, "/");
        }
        strcat(dst, components[i]);
    }
    
    // Handle root path
    if (resolved[0] == '/' && dst == resolved + 1 && *dst == '\0') {
        // Just root
    }
    
    // Re-add leading / if it was there
    if (base_copy[0] == '/' || path_copy[0] == '/') {
        memmove(resolved + 1, resolved, strlen(resolved) + 1);
        resolved[0] = '/';
    }
}

/**
 * @brief Parse #EXTINF line to extract duration and title
 * 
 * Format: #EXTINF:duration,title
 * Example: #EXTINF:180,Artist - Track Title
 */
static void parse_extinf(const char *extinf, uint32_t *duration, char *title, size_t title_size) {
    if (!extinf || !duration || !title || title_size == 0) return;
    
    *duration = 0;
    title[0] = '\0';
    
    // Find comma separator
    const char *comma = strchr(extinf, ',');
    
    if (comma) {
        // Parse duration (everything before comma)
        char duration_str[16];
        size_t dur_len = comma - extinf;
        if (dur_len >= sizeof(duration_str)) {
            dur_len = sizeof(duration_str) - 1;
        }
        strncpy(duration_str, extinf, dur_len);
        duration_str[dur_len] = '\0';
        
        // Handle negative durations (some extended M3U variants)
        int dur = atoi(duration_str);
        *duration = (dur > 0) ? (uint32_t)dur : 0;
        
        // Copy title (everything after comma)
        const char *title_start = comma + 1;
        while (*title_start && isspace((unsigned char)*title_start)) {
            title_start++;
        }
        strncpy(title, title_start, title_size - 1);
        title[title_size - 1] = '\0';
    } else {
        // No comma, try to parse as duration only
        int dur = atoi(extinf);
        *duration = (dur > 0) ? (uint32_t)dur : 0;
    }
}

bool m3u_is_playlist_file(const char *path) {
    if (!path) return false;
    
    size_t len = strlen(path);
    if (len < 4) return false;
    
    // Check for .m3u extension (case-insensitive)
    const char *ext = path + len - 4;
    if (strcasecmp(ext, ".m3u") == 0) return true;
    
    // Check for .m3u8 extension
    if (len >= 5) {
        ext = path + len - 5;
        if (strcasecmp(ext, ".m3u8") == 0) return true;
    }
    
    return false;
}

bool m3u_parse_line(const char *line, const char *base_dir, m3u_entry_t *entry) {
    if (!line || !entry) return false;
    
    // Make mutable copy and trim
    char line_copy[512];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';
    trim_whitespace(line_copy);
    
    // Skip empty lines and comments
    if (line_copy[0] == '\0' || line_copy[0] == '#') {
        return false;
    }
    
    // This is a track path
    memset(entry, 0, sizeof(m3u_entry_t));
    resolve_path(line_copy, base_dir, entry->path, sizeof(entry->path));
    extract_filename_no_ext(entry->path, entry->title, sizeof(entry->title));
    
    return true;
}

m3u_playlist_t* m3u_parse_file(const char *m3u_path) {
    if (!m3u_path) return NULL;
    
    FILE *f = fopen(m3u_path, "r");
    if (!f) return NULL;
    
    // Allocate playlist
    m3u_playlist_t *playlist = calloc(1, sizeof(m3u_playlist_t));
    if (!playlist) {
        fclose(f);
        return NULL;
    }
    
    playlist->entries = calloc(MAX_PLAYLIST_TRACKS, sizeof(m3u_entry_t));
    if (!playlist->entries) {
        free(playlist);
        fclose(f);
        return NULL;
    }
    playlist->capacity = MAX_PLAYLIST_TRACKS;
    
    // Extract playlist name from filename (without extension)
    extract_filename_no_ext(m3u_path, playlist->name, sizeof(playlist->name));
    
    // Store full path
    strncpy(playlist->path, m3u_path, sizeof(playlist->path) - 1);
    playlist->path[sizeof(playlist->path) - 1] = '\0';
    
    // Get base directory for resolving relative paths
    char base_dir[MAX_TRACK_PATH_LEN];
    get_directory(m3u_path, base_dir, sizeof(base_dir));
    
    // Parse the file
    char line[512];
    char pending_title[MAX_TRACK_TITLE_LEN] = {0};
    uint32_t pending_duration = 0;
    
    while (fgets(line, sizeof(line), f)) {
        trim_whitespace(line);
        
        // Skip empty lines
        if (line[0] == '\0') continue;
        
        // Check for #EXTM3U header (just skip it)
        if (strcmp(line, "#EXTM3U") == 0) {
            continue;
        }
        
        // Parse #EXTINF metadata
        if (strncmp(line, "#EXTINF:", 8) == 0) {
            parse_extinf(line + 8, &pending_duration, pending_title, sizeof(pending_title));
            continue;
        }
        
        // Skip other comments/directives
        if (line[0] == '#') {
            continue;
        }
        
        // This is a track path
        if (playlist->entry_count >= playlist->capacity) {
            break;  // Playlist full
        }
        
        m3u_entry_t *entry = &playlist->entries[playlist->entry_count];
        memset(entry, 0, sizeof(m3u_entry_t));
        
        // Resolve the path
        resolve_path(line, base_dir, entry->path, sizeof(entry->path));
        
        // Use pending title if available, otherwise extract from filename
        if (pending_title[0]) {
            strncpy(entry->title, pending_title, sizeof(entry->title) - 1);
            entry->title[sizeof(entry->title) - 1] = '\0';
        } else {
            extract_filename_no_ext(entry->path, entry->title, sizeof(entry->title));
        }
        
        entry->duration_seconds = pending_duration;
        playlist->entry_count++;
        
        // Reset pending metadata
        pending_title[0] = '\0';
        pending_duration = 0;
    }
    
    fclose(f);
    return playlist;
}

void m3u_playlist_free(m3u_playlist_t *playlist) {
    if (!playlist) return;
    
    if (playlist->entries) {
        free(playlist->entries);
    }
    free(playlist);
}
