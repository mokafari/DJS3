/**
 * @file metadata.c
 * @brief OpenDeck metadata manager implementation
 * 
 * Provides mutex-protected load/save operations for .odk metadata files.
 * Includes recursive directory creation for FATFS compatibility.
 */

#include "metadata.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>  // For unlink(), rename()
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "METADATA";

// Mount point and hidden directory for metadata
static const char *MOUNT_POINT = "/sdcard";
static const char *HIDDEN_DIR = "/sdcard/.opendeck";

// Mutex for thread-safe file access
static SemaphoreHandle_t file_lock = NULL;

/**
 * @brief Initialize metadata manager
 */
void metadata_init(void) {
    if (!file_lock) {
        file_lock = xSemaphoreCreateMutex();
        if (file_lock) {
            ESP_LOGI(TAG, "Metadata manager initialized");
        } else {
            ESP_LOGE(TAG, "Failed to create file lock mutex");
        }
    }
}

/**
 * @brief Recursive mkdir (Essential for FATFS - no native support)
 * 
 * Creates all directories in the path, e.g.:
 * "/sdcard/.opendeck/Music/Techno" creates:
 * 1. /sdcard/.opendeck
 * 2. /sdcard/.opendeck/Music
 * 3. /sdcard/.opendeck/Music/Techno
 * 
 * @param path Directory path to create
 */
static void mkdir_p(char *path) {
    char temp[256];
    char *p = NULL;
    size_t len;

    snprintf(temp, sizeof(temp), "%s", path);
    len = strlen(temp);
    
    // Remove trailing slash if present
    if (len > 0 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }

    // Iterate through path, creating directories as we go
    for (p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            // Attempt to create dir; ignore error if exists
            mkdir(temp, 0755);
            *p = '/';
        }
    }
    // Create final directory
    mkdir(temp, 0755);
}

/**
 * @brief Convert MP3 path to ODK path (safe version with bounds checking)
 * 
 * In:  /sdcard/Music/Song.mp3
 * Out: /sdcard/.opendeck/Music/Song.odk
 * 
 * @return true if path constructed successfully, false on truncation
 */
bool metadata_get_path(const char *mp3_path, char *out_path, size_t out_size) {
    if (!mp3_path || !out_path || out_size < 10) {
        ESP_LOGE(TAG, "Invalid parameters for path conversion");
        return false;
    }
    
    int written;
    
    // Check if path starts with mount point
    if (strncmp(mp3_path, MOUNT_POINT, strlen(MOUNT_POINT)) == 0) {
        // Construct: /sdcard/.opendeck + /Music/Song.mp3
        written = snprintf(out_path, out_size, "%s%s", HIDDEN_DIR, mp3_path + strlen(MOUNT_POINT));
    } else {
        // Path doesn't start with mount point, prepend hidden dir
        written = snprintf(out_path, out_size, "%s/%s", HIDDEN_DIR, mp3_path);
    }
    
    // Check for truncation (need room for extension replacement)
    if (written < 0 || (size_t)written >= out_size) {
        ESP_LOGE(TAG, "Path too long: %s", mp3_path);
        out_path[0] = '\0';
        return false;
    }

    // Replace extension with .odk (safely)
    char *ext = strrchr(out_path, '.');
    char *slash = strrchr(out_path, '/');
    
    // Only replace if extension is after the last slash (it's a file extension, not a dot in path)
    if (ext && (!slash || ext > slash)) {
        // Check we have room for ".odk" (4 chars + null)
        size_t base_len = ext - out_path;
        if (base_len + 5 > out_size) {
            ESP_LOGE(TAG, "Path too long for extension: %s", mp3_path);
            out_path[0] = '\0';
            return false;
        }
        strcpy(ext, ".odk");
    } else {
        // No extension - append .odk
        size_t len = strlen(out_path);
        if (len + 5 > out_size) {
            ESP_LOGE(TAG, "Path too long for extension: %s", mp3_path);
            out_path[0] = '\0';
            return false;
        }
        strcat(out_path, ".odk");
    }
    
    return true;
}

/**
 * @brief Check if metadata file exists
 */
bool metadata_exists(const char *mp3_path) {
    char odk_path[256];
    if (!metadata_get_path(mp3_path, odk_path, sizeof(odk_path))) {
        return false;
    }
    
    struct stat st;
    return (stat(odk_path, &st) == 0);
}

/**
 * @brief Get base directory for metadata
 */
const char* metadata_get_base_dir(void) {
    return HIDDEN_DIR;
}

/**
 * @brief Get .odk path for an MP3 file (alias for metadata_get_path)
 */
bool metadata_get_odk_path(const char *mp3_path, char *odk_path, size_t odk_path_len) {
    return metadata_get_path(mp3_path, odk_path, odk_path_len);
}

/**
 * @brief Create parent directories for a file path
 * 
 * Extracts directory portion from path and creates it recursively.
 * 
 * @param file_path Full path to a file (e.g., "/sdcard/.opendeck/Music/Track.odk")
 */
static void create_parent_dirs(const char *file_path) {
    char dir_only[256];
    strncpy(dir_only, file_path, sizeof(dir_only) - 1);
    dir_only[sizeof(dir_only) - 1] = '\0';
    
    char *slash = strrchr(dir_only, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(dir_only);
    }
}

/**
 * @brief Save metadata for a track (atomic write)
 * 
 * Uses atomic write pattern:
 * 1. Write to .odk.tmp temporary file
 * 2. Rename to .odk on success
 * 3. This prevents corrupt files if power is lost mid-write
 */
bool metadata_save(const char *mp3_path, const TrackMetadata_t *data) {
    // Lazy init and verify mutex exists
    if (!file_lock) {
        metadata_init();
        if (!file_lock) {
            ESP_LOGE(TAG, "Cannot save: mutex creation failed");
            return false;
        }
    }
    
    char odk_path[256];
    char tmp_path[260];  // Extra room for ".tmp" suffix
    
    if (!metadata_get_path(mp3_path, odk_path, sizeof(odk_path))) {
        return false;
    }
    
    // Build temp file path
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", odk_path);
    
    // 1. Acquire lock (500ms timeout)
    if (xSemaphoreTake(file_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "File lock timeout on save");
        return false;
    }

    // 2. Ensure parent directories exist
    create_parent_dirs(odk_path);

    // 3. Write to temp file first (atomic write pattern)
    bool success = false;
    FILE *f = fopen(tmp_path, "wb");
    if (f) {
        size_t written = fwrite(data, 1, sizeof(TrackMetadata_t), f);
        fclose(f);
        
        if (written == sizeof(TrackMetadata_t)) {
            // 4. Atomic rename: temp -> final
            if (rename(tmp_path, odk_path) == 0) {
                success = true;
                ESP_LOGI(TAG, "Saved metadata: %s", odk_path);
            } else {
                ESP_LOGE(TAG, "Failed to rename %s -> %s", tmp_path, odk_path);
                unlink(tmp_path);  // Clean up temp file
            }
        } else {
            ESP_LOGE(TAG, "Write incomplete: %zu/%zu bytes", written, sizeof(TrackMetadata_t));
            unlink(tmp_path);  // Clean up temp file
        }
    } else {
        ESP_LOGE(TAG, "Failed to open for writing: %s", tmp_path);
    }

    // 5. Release lock
    xSemaphoreGive(file_lock);
    return success;
}

/**
 * @brief Load metadata for a track
 */
bool metadata_load(const char *mp3_path, TrackMetadata_t *out_data) {
    // Lazy init and verify mutex exists
    if (!file_lock) {
        metadata_init();
        if (!file_lock) {
            ESP_LOGE(TAG, "Cannot load: mutex creation failed");
            return false;
        }
    }

    char odk_path[256];
    if (!metadata_get_path(mp3_path, odk_path, sizeof(odk_path))) {
        return false;
    }

    // Acquire lock (100ms timeout for reads)
    if (xSemaphoreTake(file_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "File lock timeout on load");
        return false;
    }

    FILE *f = fopen(odk_path, "rb");
    if (!f) {
        xSemaphoreGive(file_lock);
        return false;  // File doesn't exist - not an error
    }

    size_t read_bytes = fread(out_data, 1, sizeof(TrackMetadata_t), f);
    fclose(f);
    xSemaphoreGive(file_lock);

    // Validate magic number and version
    if (read_bytes != sizeof(TrackMetadata_t)) {
        ESP_LOGW(TAG, "Incomplete read: %zu/%zu bytes from %s", 
                 read_bytes, sizeof(TrackMetadata_t), odk_path);
        return false;
    }
    
    if (out_data->magic != ODK_MAGIC) {
        ESP_LOGW(TAG, "Invalid magic: 0x%08X (expected 0x%08X)", 
                 out_data->magic, ODK_MAGIC);
        return false;
    }
    
    if (out_data->version != ODK_VERSION) {
        ESP_LOGW(TAG, "Version mismatch: %u (expected %u)", 
                 out_data->version, ODK_VERSION);
        return false;
    }

    ESP_LOGI(TAG, "Loaded metadata: %s (BPM: %.1f, Duration: %ums)", 
             odk_path, out_data->bpm, out_data->duration_ms);
    return true;
}

/**
 * @brief Update hot cues in metadata file
 */
bool metadata_update_hotcues(const char *mp3_path, const HotCue_t *hotcues) {
    TrackMetadata_t meta;
    
    // Load existing metadata
    if (!metadata_load(mp3_path, &meta)) {
        ESP_LOGW(TAG, "Cannot update hotcues - no metadata for %s", mp3_path);
        return false;
    }
    
    // Update hot cues
    memcpy(meta.hotcues, hotcues, sizeof(HotCue_t) * NUM_HOTCUES);
    
    // Save back
    return metadata_save(mp3_path, &meta);
}
