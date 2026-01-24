/**
 * @file track_db.c
 * @brief Track database implementation with ID3 tag parsing
 */

#include "track_db.h"
#include "storage.h"
#include "id3_parser.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "track_db";
static track_info_t tracks[MAX_TRACKS];
static uint32_t track_count = 0;

/**
 * @brief Parse ID3v2 tag using ID3 parser
 */
static void parse_id3_tag(const char *filepath, track_info_t *info) {
    id3_tag_t tag;
    if (id3_parse_file(filepath, &tag)) {
        info->has_id3 = true;
        
        // Copy title
        if (tag.title[0] != '\0') {
            strncpy(info->title, tag.title, sizeof(info->title) - 1);
            info->title[sizeof(info->title) - 1] = '\0';
        }
        
        // Copy artist
        if (tag.artist[0] != '\0') {
            strncpy(info->artist, tag.artist, sizeof(info->artist) - 1);
            info->artist[sizeof(info->artist) - 1] = '\0';
        }
    } else {
        info->has_id3 = false;
    }
}

/**
 * @brief Check if file is MP3 by extension
 */
static bool is_mp3_file(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4) {
        return false;
    }
    const char *ext = filename + len - 4;
    return (strcasecmp(ext, ".mp3") == 0);
}

/**
 * @brief Scan directory for MP3 files
 */
static uint32_t scan_directory(const char *path, uint32_t start_index) {
    DIR *dir = opendir(path);
    if (!dir) {
        return 0;
    }
    
    uint32_t count = start_index;
    struct dirent *entry;
    char full_path[512];
    
    while ((entry = readdir(dir)) != NULL && count < MAX_TRACKS) {
        // Skip . and ..
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // Build full path
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISREG(st.st_mode) && is_mp3_file(entry->d_name)) {
                // Found MP3 file
                track_info_t *track = &tracks[count];
                strncpy(track->filename, entry->d_name, sizeof(track->filename) - 1);
                track->filename[sizeof(track->filename) - 1] = '\0';
                track->file_size = st.st_size;
                track->duration_seconds = 0; // TODO: Parse from MP3
                track->has_id3 = false;
                
                // Try to parse ID3 tag
                snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
                parse_id3_tag(full_path, track);
                
                // Use filename as title if no ID3
                if (!track->has_id3 || track->title[0] == '\0') {
                    strncpy(track->title, entry->d_name, sizeof(track->title) - 1);
                    // Remove .mp3 extension
                    size_t len = strlen(track->title);
                    if (len > 4 && strcasecmp(track->title + len - 4, ".mp3") == 0) {
                        track->title[len - 4] = '\0';
                    }
                }
                
                count++;
            } else if (S_ISDIR(st.st_mode)) {
                // Recursively scan subdirectories
                count += scan_directory(full_path, count);
            }
        }
    }
    
    closedir(dir);
    return count - start_index;
}

bool track_db_init(void) {
    ESP_LOGI(TAG, "Initializing track database");
    track_count = 0;
    memset(tracks, 0, sizeof(tracks));
    return true;
}

uint32_t track_db_scan(void) {
    ESP_LOGI(TAG, "Scanning for MP3 files...");
    
    track_db_clear();
    
    if (!storage_is_available()) {
        ESP_LOGW(TAG, "No storage available for scanning");
        return 0;
    }
    
    const char *mount_point = storage_get_mount_point();
    if (!mount_point) {
        ESP_LOGW(TAG, "Storage not mounted");
        return 0;
    }
    
    uint32_t found = scan_directory(mount_point, 0);
    track_count = found;
    
    ESP_LOGI(TAG, "Found %d MP3 tracks", track_count);
    return track_count;
}

uint32_t track_db_get_count(void) {
    return track_count;
}

bool track_db_get_track(uint32_t index, track_info_t *info) {
    if (index >= track_count || !info) {
        return false;
    }
    
    *info = tracks[index];
    return true;
}

bool track_db_find_by_filename(const char *filename, track_info_t *info) {
    if (!filename || !info) {
        return false;
    }
    
    for (uint32_t i = 0; i < track_count; i++) {
        if (strcmp(tracks[i].filename, filename) == 0) {
            *info = tracks[i];
            return true;
        }
    }
    
    return false;
}

void track_db_clear(void) {
    track_count = 0;
    memset(tracks, 0, sizeof(tracks));
}

