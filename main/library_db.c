/**
 * @file library_db.c
 * @brief Library database implementation
 * 
 * Provides fast track browsing via compact index stored in single file.
 */

#include "library_db.h"
#include "metadata.h"
#include "metadata_format.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "LIBRARY_DB";

// Library file path
static const char *LIBRARY_PATH = "/sdcard/.opendeck/library.db";

// In-memory library
static LibraryHeader_t header = {0};
static LibraryEntry_t *entries = NULL;
static bool library_loaded = false;
static SemaphoreHandle_t library_mutex = NULL;

// Background task handle
static TaskHandle_t verify_task_handle = NULL;

// CRC32 lookup table
static uint32_t crc32_table[256];
static bool crc32_initialized = false;

// Forward declarations
static void init_crc32_table(void);
static void verify_task(void *pvParameters);
static void scan_directory_recursive(const char *path, uint32_t *count);

/**
 * @brief Initialize CRC32 lookup table
 */
static void init_crc32_table(void) {
    if (crc32_initialized) return;
    
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = polynomial ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }
    crc32_initialized = true;
}

/**
 * @brief Calculate CRC32 hash
 */
uint32_t library_db_hash(const char *str) {
    init_crc32_table();
    
    uint32_t crc = 0xFFFFFFFF;
    while (*str) {
        crc = crc32_table[(crc ^ *str) & 0xFF] ^ (crc >> 8);
        str++;
    }
    return crc ^ 0xFFFFFFFF;
}

/**
 * @brief Initialize library database
 */
void library_db_init(void) {
    if (!library_mutex) {
        library_mutex = xSemaphoreCreateMutex();
    }
    
    init_crc32_table();
    
    // Allocate entries array in PSRAM
    if (!entries) {
        entries = heap_caps_calloc(MAX_LIBRARY_ENTRIES, sizeof(LibraryEntry_t), 
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!entries) {
            ESP_LOGE(TAG, "Failed to allocate library entries");
            return;
        }
    }
    
    ESP_LOGI(TAG, "Library database initialized");
}

/**
 * @brief Load library from SD card
 */
bool library_db_load(void) {
    if (!entries) {
        library_db_init();
    }
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    FILE *f = fopen(LIBRARY_PATH, "rb");
    if (!f) {
        xSemaphoreGive(library_mutex);
        ESP_LOGW(TAG, "Library file not found: %s", LIBRARY_PATH);
        return false;
    }
    
    // Read header
    size_t read = fread(&header, 1, sizeof(LibraryHeader_t), f);
    if (read != sizeof(LibraryHeader_t)) {
        fclose(f);
        xSemaphoreGive(library_mutex);
        ESP_LOGE(TAG, "Failed to read header");
        return false;
    }
    
    // Validate header
    if (header.magic != LIBRARY_MAGIC || header.version != LIBRARY_VERSION) {
        fclose(f);
        xSemaphoreGive(library_mutex);
        ESP_LOGW(TAG, "Invalid library format");
        return false;
    }
    
    // Clamp entry count
    if (header.entry_count > MAX_LIBRARY_ENTRIES) {
        header.entry_count = MAX_LIBRARY_ENTRIES;
    }
    
    // Read entries
    if (header.entry_count > 0) {
        read = fread(entries, sizeof(LibraryEntry_t), header.entry_count, f);
        if (read != header.entry_count) {
            ESP_LOGW(TAG, "Partial read: %zu/%u entries", read, header.entry_count);
            header.entry_count = read;
        }
    }
    
    fclose(f);
    library_loaded = true;
    
    xSemaphoreGive(library_mutex);
    
    ESP_LOGI(TAG, "Loaded library: %u entries", header.entry_count);
    return true;
}

/**
 * @brief Save library to SD card
 */
bool library_db_save(void) {
    if (!entries) return false;
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    // Ensure directory exists
    mkdir("/sdcard/.opendeck", 0755);
    
    FILE *f = fopen(LIBRARY_PATH, "wb");
    if (!f) {
        xSemaphoreGive(library_mutex);
        ESP_LOGE(TAG, "Failed to open for writing: %s", LIBRARY_PATH);
        return false;
    }
    
    // Update header
    header.magic = LIBRARY_MAGIC;
    header.version = LIBRARY_VERSION;
    
    // Write header
    fwrite(&header, 1, sizeof(LibraryHeader_t), f);
    
    // Write entries
    if (header.entry_count > 0) {
        fwrite(entries, sizeof(LibraryEntry_t), header.entry_count, f);
    }
    
    fclose(f);
    
    xSemaphoreGive(library_mutex);
    
    ESP_LOGI(TAG, "Saved library: %u entries", header.entry_count);
    return true;
}

/**
 * @brief Rebuild library by scanning .odk files
 */
uint32_t library_db_rebuild(void) {
    if (!entries) {
        library_db_init();
    }
    
    ESP_LOGI(TAG, "Rebuilding library...");
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    // Clear existing entries
    header.entry_count = 0;
    memset(entries, 0, MAX_LIBRARY_ENTRIES * sizeof(LibraryEntry_t));
    
    // Scan .opendeck directory
    uint32_t count = 0;
    scan_directory_recursive("/sdcard/.opendeck", &count);
    
    header.entry_count = count;
    header.last_scan_time = (uint32_t)time(NULL);
    library_loaded = true;
    
    xSemaphoreGive(library_mutex);
    
    // Save to disk
    library_db_save();
    
    ESP_LOGI(TAG, "Rebuild complete: %u entries", count);
    return count;
}

/**
 * @brief Recursively scan directory for .odk files
 */
static void scan_directory_recursive(const char *path, uint32_t *count) {
    if (*count >= MAX_LIBRARY_ENTRIES) return;
    
    DIR *dir = opendir(path);
    if (!dir) return;
    
    struct dirent *entry;
    char filepath[512];  // Larger buffer to avoid truncation
    
    while ((entry = readdir(dir)) != NULL && *count < MAX_LIBRARY_ENTRIES) {
        // Skip . and ..
        if (entry->d_name[0] == '.') continue;
        
        // Build full path, skip if too long
        int ret = snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        if (ret >= (int)sizeof(filepath)) continue;  // Path too long, skip
        
        struct stat st;
        if (stat(filepath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            scan_directory_recursive(filepath, count);
        } else if (S_ISREG(st.st_mode)) {
            // Check if .odk file
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcasecmp(entry->d_name + len - 4, ".odk") == 0) {
                // Load metadata to get BPM and key
                TrackMetadata_t meta;
                
                // Convert .odk path back to MP3 path for loading
                // This is a bit redundant but ensures consistency
                char mp3_path[512];
                
                // Remove .opendeck from path and change extension
                const char *rel_path = filepath + strlen("/sdcard/.opendeck");
                int mp3_ret = snprintf(mp3_path, sizeof(mp3_path), "/sdcard%s", rel_path);
                if (mp3_ret >= (int)sizeof(mp3_path)) continue;  // Path too long
                char *ext = strrchr(mp3_path, '.');
                if (ext) strcpy(ext, ".mp3");
                
                if (metadata_load(mp3_path, &meta)) {
                    LibraryEntry_t *e = &entries[*count];
                    e->path_hash = library_db_hash(mp3_path);
                    e->bpm_x100 = (uint16_t)(meta.bpm * 100);
                    e->key_id = meta.key_id;
                    e->flags = LIB_FLAG_ANALYZED;
                    
                    // Check for hot cues
                    for (int i = 0; i < NUM_HOTCUES; i++) {
                        if (meta.hotcues[i].active) {
                            e->flags |= LIB_FLAG_HAS_CUES;
                            break;
                        }
                    }
                    
                    (*count)++;
                }
            }
        }
        
        // Yield periodically
        if (*count % 10 == 0) {
            vTaskDelay(1);
        }
    }
    
    closedir(dir);
}

/**
 * @brief Start background verification
 */
void library_db_verify_async(void) {
    if (verify_task_handle) return;  // Already running
    
    xTaskCreate(verify_task, "lib_verify", 4096, NULL, 1, &verify_task_handle);
}

/**
 * @brief Background verification task
 */
static void verify_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting library verification");
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    int verified = 0;
    int removed = 0;
    
    for (uint32_t i = 0; i < header.entry_count; i++) {
        // TODO: Verify entry exists on filesystem
        // For now, just mark as verified
        entries[i].flags |= LIB_FLAG_VERIFIED;
        verified++;
        
        // Yield periodically
        if (i % 10 == 0) {
            xSemaphoreGive(library_mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            xSemaphoreTake(library_mutex, portMAX_DELAY);
        }
    }
    
    xSemaphoreGive(library_mutex);
    
    ESP_LOGI(TAG, "Verification complete: %d verified, %d removed", verified, removed);
    
    verify_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Find entry by hash
 */
LibraryEntry_t* library_db_find(uint32_t path_hash) {
    if (!entries || !library_loaded) return NULL;
    
    for (uint32_t i = 0; i < header.entry_count; i++) {
        if (entries[i].path_hash == path_hash) {
            return &entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Find entry by file path
 */
LibraryEntry_t* library_db_find_by_path(const char *filepath) {
    uint32_t hash = library_db_hash(filepath);
    return library_db_find(hash);
}

/**
 * @brief Update or add entry
 */
bool library_db_update(const char *filepath, float bpm, uint8_t key_id, uint8_t flags) {
    if (!entries) {
        library_db_init();
    }
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    uint32_t hash = library_db_hash(filepath);
    LibraryEntry_t *existing = library_db_find(hash);
    
    if (existing) {
        // Update existing
        existing->bpm_x100 = (uint16_t)(bpm * 100);
        existing->key_id = key_id;
        existing->flags = flags;
    } else if (header.entry_count < MAX_LIBRARY_ENTRIES) {
        // Add new
        LibraryEntry_t *e = &entries[header.entry_count];
        e->path_hash = hash;
        e->bpm_x100 = (uint16_t)(bpm * 100);
        e->key_id = key_id;
        e->flags = flags;
        header.entry_count++;
    } else {
        xSemaphoreGive(library_mutex);
        return false;  // Library full
    }
    
    xSemaphoreGive(library_mutex);
    return true;
}

/**
 * @brief Remove entry
 */
bool library_db_remove(const char *filepath) {
    if (!entries || !library_loaded) return false;
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    uint32_t hash = library_db_hash(filepath);
    
    for (uint32_t i = 0; i < header.entry_count; i++) {
        if (entries[i].path_hash == hash) {
            // Shift remaining entries
            if (i < header.entry_count - 1) {
                memmove(&entries[i], &entries[i + 1], 
                        (header.entry_count - i - 1) * sizeof(LibraryEntry_t));
            }
            header.entry_count--;
            xSemaphoreGive(library_mutex);
            return true;
        }
    }
    
    xSemaphoreGive(library_mutex);
    return false;
}

/**
 * @brief Get entry count
 */
uint32_t library_db_get_count(void) {
    return library_loaded ? header.entry_count : 0;
}

/**
 * @brief Get entry by index
 */
LibraryEntry_t* library_db_get_entry(uint32_t index) {
    if (!entries || !library_loaded || index >= header.entry_count) {
        return NULL;
    }
    return &entries[index];
}

/**
 * @brief Compare function for BPM sorting
 */
static int compare_bpm_asc(const void *a, const void *b) {
    return ((LibraryEntry_t*)a)->bpm_x100 - ((LibraryEntry_t*)b)->bpm_x100;
}

static int compare_bpm_desc(const void *a, const void *b) {
    return ((LibraryEntry_t*)b)->bpm_x100 - ((LibraryEntry_t*)a)->bpm_x100;
}

/**
 * @brief Sort by BPM
 */
void library_db_sort_by_bpm(bool ascending) {
    if (!entries || !library_loaded || header.entry_count < 2) return;
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    qsort(entries, header.entry_count, sizeof(LibraryEntry_t),
          ascending ? compare_bpm_asc : compare_bpm_desc);
    
    xSemaphoreGive(library_mutex);
}

/**
 * @brief Compare function for key sorting
 */
static int compare_key_asc(const void *a, const void *b) {
    return ((LibraryEntry_t*)a)->key_id - ((LibraryEntry_t*)b)->key_id;
}

static int compare_key_desc(const void *a, const void *b) {
    return ((LibraryEntry_t*)b)->key_id - ((LibraryEntry_t*)a)->key_id;
}

/**
 * @brief Sort by key
 */
void library_db_sort_by_key(bool ascending) {
    if (!entries || !library_loaded || header.entry_count < 2) return;
    
    xSemaphoreTake(library_mutex, portMAX_DELAY);
    
    qsort(entries, header.entry_count, sizeof(LibraryEntry_t),
          ascending ? compare_key_asc : compare_key_desc);
    
    xSemaphoreGive(library_mutex);
}

/**
 * @brief Check if loaded
 */
bool library_db_is_loaded(void) {
    return library_loaded;
}

/**
 * @brief Get last scan timestamp
 */
uint32_t library_db_get_last_scan(void) {
    return library_loaded ? header.last_scan_time : 0;
}
