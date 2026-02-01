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
 * @brief Convert MP3 path to ODK path
 * 
 * In:  /sdcard/Music/Song.mp3
 * Out: /sdcard/.opendeck/Music/Song.odk
 */
void metadata_get_path(const char *mp3_path, char *out_path) {
    // Check if path starts with mount point
    if (strncmp(mp3_path, MOUNT_POINT, strlen(MOUNT_POINT)) == 0) {
        // Construct: /sdcard/.opendeck + /Music/Song.mp3
        sprintf(out_path, "%s%s", HIDDEN_DIR, mp3_path + strlen(MOUNT_POINT));
    } else {
        // Path doesn't start with mount point, prepend hidden dir
        sprintf(out_path, "%s/%s", HIDDEN_DIR, mp3_path);
    }

    // Replace extension with .odk
    char *ext = strrchr(out_path, '.');
    if (ext) {
        strcpy(ext, ".odk");
    } else {
        strcat(out_path, ".odk");
    }
}

/**
 * @brief Check if metadata file exists
 */
bool metadata_exists(const char *mp3_path) {
    char odk_path[256];
    metadata_get_path(mp3_path, odk_path);
    
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
 * @brief Save metadata for a track
 */
bool metadata_save(const char *mp3_path, const TrackMetadata_t *data) {
    // Lazy init
    if (!file_lock) {
        metadata_init();
    }
    
    char odk_path[256];
    metadata_get_path(mp3_path, odk_path);
    
    // 1. Acquire lock (500ms timeout)
    if (xSemaphoreTake(file_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "File lock timeout on save");
        return false;
    }

    // 2. Ensure directory exists
    char dir_only[256];
    strcpy(dir_only, odk_path);
    char *slash = strrchr(dir_only, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(dir_only);
    }

    // 3. Write file
    bool success = false;
    FILE *f = fopen(odk_path, "wb");
    if (f) {
        size_t written = fwrite(data, 1, sizeof(TrackMetadata_t), f);
        if (written == sizeof(TrackMetadata_t)) {
            success = true;
            ESP_LOGI(TAG, "Saved metadata: %s", odk_path);
        } else {
            ESP_LOGE(TAG, "Write incomplete: %zu/%zu bytes", written, sizeof(TrackMetadata_t));
        }
        fclose(f);
    } else {
        ESP_LOGE(TAG, "Failed to open for writing: %s", odk_path);
    }

    // 4. Release lock
    xSemaphoreGive(file_lock);
    return success;
}

/**
 * @brief Load metadata for a track
 */
bool metadata_load(const char *mp3_path, TrackMetadata_t *out_data) {
    // Lazy init
    if (!file_lock) {
        metadata_init();
    }

    char odk_path[256];
    metadata_get_path(mp3_path, odk_path);

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
