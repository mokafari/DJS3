#include "m3u_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

m3u_playlist_t* m3u_playlist_create(const char *name) {
    m3u_playlist_t *playlist = calloc(1, sizeof(m3u_playlist_t));
    if (!playlist) return NULL;
    
    strncpy(playlist->name, name, sizeof(playlist->name) - 1);
    playlist->entries = calloc(MAX_PLAYLIST_TRACKS, sizeof(m3u_entry_t));
    if (!playlist->entries) {
        free(playlist);
        return NULL;
    }
    playlist->capacity = MAX_PLAYLIST_TRACKS;
    playlist->entry_count = 0;
    
    return playlist;
}

bool m3u_playlist_add_track(m3u_playlist_t *playlist, const char *path,
                            const char *title, uint32_t duration) {
    if (!playlist || !path) return false;
    if (playlist->entry_count >= playlist->capacity) return false;
    
    m3u_entry_t *entry = &playlist->entries[playlist->entry_count];
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    
    if (title) {
        strncpy(entry->title, title, sizeof(entry->title) - 1);
    } else {
        // Extract filename as title
        const char *slash = strrchr(path, '/');
        const char *name = slash ? slash + 1 : path;
        strncpy(entry->title, name, sizeof(entry->title) - 1);
        // Remove extension
        char *dot = strrchr(entry->title, '.');
        if (dot) *dot = '\0';
    }
    
    entry->duration_seconds = duration;
    playlist->entry_count++;
    
    return true;
}

bool m3u_playlist_remove_track(m3u_playlist_t *playlist, uint32_t index) {
    if (!playlist || index >= playlist->entry_count) return false;
    
    // Shift remaining entries
    for (uint32_t i = index; i < playlist->entry_count - 1; i++) {
        playlist->entries[i] = playlist->entries[i + 1];
    }
    playlist->entry_count--;
    
    return true;
}

bool m3u_playlist_move_track(m3u_playlist_t *playlist, uint32_t from, uint32_t to) {
    if (!playlist || from >= playlist->entry_count || to >= playlist->entry_count) {
        return false;
    }
    if (from == to) return true;
    
    m3u_entry_t temp = playlist->entries[from];
    
    if (from < to) {
        for (uint32_t i = from; i < to; i++) {
            playlist->entries[i] = playlist->entries[i + 1];
        }
    } else {
        for (uint32_t i = from; i > to; i--) {
            playlist->entries[i] = playlist->entries[i - 1];
        }
    }
    
    playlist->entries[to] = temp;
    return true;
}

bool m3u_playlist_save(m3u_playlist_t *playlist, const char *base_dir) {
    return m3u_playlist_save_extended(playlist, base_dir);
}

bool m3u_playlist_save_extended(m3u_playlist_t *playlist, const char *base_dir) {
    if (!playlist || !base_dir) return false;
    
    char filepath[MAX_TRACK_PATH_LEN];
    
    if (playlist->path[0]) {
        strncpy(filepath, playlist->path, sizeof(filepath));
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s.m3u", base_dir, playlist->name);
    }
    
    // Write to temp file first for atomic save
    char tmp_path[MAX_TRACK_PATH_LEN + 8];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);
    
    FILE *f = fopen(tmp_path, "w");
    if (!f) return false;
    
    // Write header
    fprintf(f, "#EXTM3U\n");
    
    // Write entries
    for (uint32_t i = 0; i < playlist->entry_count; i++) {
        m3u_entry_t *entry = &playlist->entries[i];
        
        // Write #EXTINF line
        fprintf(f, "#EXTINF:%" PRIu32 ",%s\n", entry->duration_seconds, entry->title);
        
        // Write path
        fprintf(f, "%s\n", entry->path);
    }
    
    fclose(f);
    
    // Atomic rename
    if (rename(tmp_path, filepath) != 0) {
        unlink(tmp_path);
        return false;
    }
    
    // Update playlist path
    strncpy(playlist->path, filepath, sizeof(playlist->path));
    
    return true;
}
