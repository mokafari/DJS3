#include "playlist_manager.h"
#include "m3u_parser.h"
#include "m3u_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_log.h"

static const char *TAG = "playlist_mgr";

static playlist_info_t s_playlists[MAX_PLAYLISTS];
static uint32_t s_playlist_count = 0;
static m3u_playlist_t *s_current_playlist = NULL;
static int s_current_index = -1;

// Forward declaration
static uint32_t count_playlist_tracks(const char *path);

bool playlist_manager_init(void) {
    // Create playlist directory if it doesn't exist
    struct stat st;
    if (stat(PLAYLIST_DIR, &st) != 0) {
        mkdir(PLAYLIST_DIR, 0755);
        ESP_LOGI(TAG, "Created playlist directory: %s", PLAYLIST_DIR);
    }
    
    playlist_manager_rescan();
    ESP_LOGI(TAG, "Playlist manager initialized, found %"PRIu32" playlists", s_playlist_count);
    return true;
}

void playlist_manager_rescan(void) {
    s_playlist_count = 0;
    
    DIR *dir = opendir(PLAYLIST_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open playlist directory");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_playlist_count < MAX_PLAYLISTS) {
        // Check for .m3u or .m3u8 extension
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) continue;
        if (strcasecmp(ext, ".m3u") != 0 && strcasecmp(ext, ".m3u8") != 0) continue;
        
        playlist_info_t *info = &s_playlists[s_playlist_count];
        
        // Check filename length to avoid truncation
        size_t dir_len = strlen(PLAYLIST_DIR);
        size_t name_len = strlen(entry->d_name);
        if (dir_len + 1 + name_len >= sizeof(info->path)) {
            ESP_LOGW(TAG, "Skipping playlist with too long path: %s", entry->d_name);
            continue;
        }
        
        // Extract name (without extension)
        strncpy(info->name, entry->d_name, sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        char *dot = strrchr(info->name, '.');
        if (dot) *dot = '\0';
        
        // Build full path (already checked length above)
        snprintf(info->path, sizeof(info->path), "%s/%s", PLAYLIST_DIR, entry->d_name);
        
        // Quick count tracks (just count non-comment lines)
        info->track_count = count_playlist_tracks(info->path);
        info->is_loaded = false;
        
        s_playlist_count++;
    }
    
    closedir(dir);
    
    // Sort alphabetically (bubble sort - good enough for small lists)
    for (uint32_t i = 0; i < s_playlist_count; i++) {
        for (uint32_t j = i + 1; j < s_playlist_count; j++) {
            if (strcasecmp(s_playlists[i].name, s_playlists[j].name) > 0) {
                playlist_info_t tmp = s_playlists[i];
                s_playlists[i] = s_playlists[j];
                s_playlists[j] = tmp;
            }
        }
    }
}

static uint32_t count_playlist_tracks(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    
    uint32_t count = 0;
    char line[512];
    
    while (fgets(line, sizeof(line), f)) {
        // Skip empty lines and comments
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        count++;
    }
    
    fclose(f);
    return count;
}

uint32_t playlist_manager_get_count(void) {
    return s_playlist_count;
}

bool playlist_manager_get_info(uint32_t index, playlist_info_t *info) {
    if (index >= s_playlist_count || !info) return false;
    *info = s_playlists[index];
    return true;
}

m3u_playlist_t* playlist_manager_load(uint32_t index) {
    if (index >= s_playlist_count) return NULL;
    
    // Unload previous
    playlist_manager_unload();
    
    // Load new playlist
    s_current_playlist = m3u_parse_file(s_playlists[index].path);
    if (s_current_playlist) {
        s_current_index = (int)index;
        s_playlists[index].is_loaded = true;
        ESP_LOGI(TAG, "Loaded playlist: %s (%"PRIu32" tracks)", 
                 s_playlists[index].name, s_current_playlist->entry_count);
    }
    
    return s_current_playlist;
}

m3u_playlist_t* playlist_manager_get_current(void) {
    return s_current_playlist;
}

void playlist_manager_unload(void) {
    if (s_current_playlist) {
        if (s_current_index >= 0 && (uint32_t)s_current_index < s_playlist_count) {
            s_playlists[s_current_index].is_loaded = false;
        }
        m3u_playlist_free(s_current_playlist);
        s_current_playlist = NULL;
        s_current_index = -1;
    }
}

int playlist_manager_create(const char *name) {
    if (s_playlist_count >= MAX_PLAYLISTS) return -1;
    if (!name || name[0] == '\0') return -1;
    
    m3u_playlist_t *playlist = m3u_playlist_create(name);
    if (!playlist) return -1;
    
    // Save empty playlist
    if (!m3u_playlist_save(playlist, PLAYLIST_DIR)) {
        m3u_playlist_free(playlist);
        return -1;
    }
    
    // Add to list
    playlist_info_t *info = &s_playlists[s_playlist_count];
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    strncpy(info->path, playlist->path, sizeof(info->path) - 1);
    info->path[sizeof(info->path) - 1] = '\0';
    info->track_count = 0;
    info->is_loaded = false;
    
    int index = (int)s_playlist_count++;
    
    m3u_playlist_free(playlist);
    ESP_LOGI(TAG, "Created playlist: %s", name);
    
    return index;
}

bool playlist_manager_delete(uint32_t index) {
    if (index >= s_playlist_count) return false;
    
    // Unload if current
    if ((int)index == s_current_index) {
        playlist_manager_unload();
    }
    
    // Delete file
    unlink(s_playlists[index].path);
    
    ESP_LOGI(TAG, "Deleted playlist: %s", s_playlists[index].name);
    
    // Remove from array
    for (uint32_t i = index; i < s_playlist_count - 1; i++) {
        s_playlists[i] = s_playlists[i + 1];
    }
    s_playlist_count--;
    
    // Adjust current index if needed
    if (s_current_index > (int)index) {
        s_current_index--;
    }
    
    return true;
}

bool playlist_manager_save_current(void) {
    if (!s_current_playlist) return false;
    
    bool result = m3u_playlist_save(s_current_playlist, PLAYLIST_DIR);
    if (result && s_current_index >= 0 && (uint32_t)s_current_index < s_playlist_count) {
        // Update track count
        s_playlists[s_current_index].track_count = s_current_playlist->entry_count;
    }
    return result;
}

bool playlist_manager_add_track(const char *path, const char *title, uint32_t duration) {
    if (!s_current_playlist) return false;
    return m3u_playlist_add_track(s_current_playlist, path, title, duration);
}

bool playlist_manager_get_track(uint32_t index, m3u_entry_t *entry) {
    if (!s_current_playlist || !entry) return false;
    if (index >= s_current_playlist->entry_count) return false;
    *entry = s_current_playlist->entries[index];
    return true;
}
