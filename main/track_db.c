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
    id3_tag_t *tag = (id3_tag_t*)malloc(sizeof(id3_tag_t));
    if (!tag) {
        ESP_LOGW(TAG, "Failed to allocate memory for ID3 tag parsing: %s", filepath);
        info->has_id3 = false;
        return;
    }
    
    memset(tag, 0, sizeof(id3_tag_t));
    
    if (id3_parse_file(filepath, tag)) {
        info->has_id3 = true;
        
        // Copy title - ensure it's properly null-terminated
        if (tag->title[0] != '\0') {
            strncpy(info->title, tag->title, sizeof(info->title) - 1);
            info->title[sizeof(info->title) - 1] = '\0';
            ESP_LOGD(TAG, "ID3 title parsed: %s", info->title);
        } else {
            ESP_LOGD(TAG, "ID3 tag found but title is empty: %s", filepath);
        }
        
        // Copy artist
        if (tag->artist[0] != '\0') {
            strncpy(info->artist, tag->artist, sizeof(info->artist) - 1);
            info->artist[sizeof(info->artist) - 1] = '\0';
        }
    } else {
        ESP_LOGD(TAG, "No ID3 tag found or parsing failed: %s", filepath);
        info->has_id3 = false;
    }
    
    free(tag);
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
 * @brief Sanitize filename by removing non-printable characters
 */
static void sanitize_filename(char *filename, size_t max_len) {
    if (!filename || max_len == 0) return;
    
    size_t len = strlen(filename);
    size_t out_idx = 0;
    
    for (size_t i = 0; i < len && out_idx < max_len - 1; i++) {
        unsigned char c = (unsigned char)filename[i];
        // Keep printable ASCII characters (32-126) and allow some common extended chars
        if (c >= 32 && c < 127) {
            filename[out_idx++] = c;
        } else if (c == '\t' || c == '\n' || c == '\r') {
            // Replace whitespace with space
            if (out_idx > 0 && filename[out_idx - 1] != ' ') {
                filename[out_idx++] = ' ';
            }
        }
        // Skip other non-printable characters
    }
    
    filename[out_idx] = '\0';
    
    // Trim trailing spaces
    while (out_idx > 0 && filename[out_idx - 1] == ' ') {
        filename[--out_idx] = '\0';
    }
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
    // Allocate buffer on heap to save stack space
    char *full_path = (char*)malloc(512);
    if (!full_path) {
        ESP_LOGE(TAG, "Failed to allocate memory for path scanning");
        closedir(dir);
        return 0;
    }
    
    while ((entry = readdir(dir)) != NULL && count < MAX_TRACKS) {
        // Skip . and ..
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // Skip MacOS metadata files (._*)
        if (strncmp(entry->d_name, "._", 2) == 0) {
            continue;
        }
        
        // Skip files starting with just "_" (system files, invalid entries)
        if (entry->d_name[0] == '_') {
            ESP_LOGD(TAG, "Skipping file starting with '_': %s", entry->d_name);
            continue;
        }
        
        // Skip files with invalid length (too short or too long)
        size_t name_len = strlen(entry->d_name);
        if (name_len < 5 || name_len > 255) {
            ESP_LOGD(TAG, "Skipping file with invalid length (%zu): %s", name_len, entry->d_name);
            continue;
        }
        
        // Build full path
        snprintf(full_path, 512, "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISREG(st.st_mode) && is_mp3_file(entry->d_name)) {
                // Found MP3 file
                track_info_t *track = &tracks[count];
                
                // Store full path
                strncpy(track->filename, full_path, sizeof(track->filename) - 1);
                track->filename[sizeof(track->filename) - 1] = '\0';
                
                track->file_size = st.st_size;
                track->duration_seconds = 0; // TODO: Parse from MP3
                track->has_id3 = false;
                
                // Try to parse ID3 tag
                parse_id3_tag(full_path, track);
                
                // Use filename as title only if ID3 parsing completely failed
                // If ID3 exists but title is empty, try artist as fallback, then filename
                if (!track->has_id3) {
                    // No ID3 tag at all - use filename
                    strncpy(track->title, entry->d_name, sizeof(track->title) - 1);
                    track->title[sizeof(track->title) - 1] = '\0';
                    // Remove .mp3 extension
                    size_t len = strlen(track->title);
                    if (len > 4 && strcasecmp(track->title + len - 4, ".mp3") == 0) {
                        track->title[len - 4] = '\0';
                    }
                    // Sanitize filename (remove non-printable chars, handle long names)
                    sanitize_filename(track->title, sizeof(track->title));
                    ESP_LOGD(TAG, "Using filename as title: %s", track->title);
                } else if (track->title[0] == '\0') {
                    // ID3 exists but title is empty - try artist, then filename
                    if (track->artist[0] != '\0') {
                        strncpy(track->title, track->artist, sizeof(track->title) - 1);
                        track->title[sizeof(track->title) - 1] = '\0';
                        ESP_LOGD(TAG, "Using artist as title fallback: %s", track->title);
                    } else {
                        // No artist either - use filename
                        strncpy(track->title, entry->d_name, sizeof(track->title) - 1);
                        track->title[sizeof(track->title) - 1] = '\0';
                        // Remove .mp3 extension
                        size_t len = strlen(track->title);
                        if (len > 4 && strcasecmp(track->title + len - 4, ".mp3") == 0) {
                            track->title[len - 4] = '\0';
                        }
                        // Sanitize filename
                        sanitize_filename(track->title, sizeof(track->title));
                        ESP_LOGD(TAG, "Using filename as title (ID3 exists but empty): %s", track->title);
                    }
                } else {
                    ESP_LOGD(TAG, "Using ID3 title: %s", track->title);
                }
                
                count++;
            } else if (S_ISDIR(st.st_mode)) {
                // Recursively scan subdirectories
                count += scan_directory(full_path, count);
            }
        }
    }
    
    free(full_path);
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

