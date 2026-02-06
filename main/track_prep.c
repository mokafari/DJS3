/**
 * @file track_prep.c
 * @brief Track preparation and pre-gig analysis implementation
 * 
 * Provides batch analysis and library verification for pre-gig preparation.
 * Uses analysis_task for actual track analysis.
 */

#include "track_prep.h"
#include "analysis_task.h"
#include "metadata.h"
#include "metadata_format.h"
#include "library_db.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "track_prep";

// Task configuration
#define PREP_TASK_STACK     8192
#define PREP_TASK_PRIORITY  3       // Slightly above analysis task
#define PREP_CORE           0       // Same core as analysis

// Default scan paths
#define DEFAULT_MUSIC_PATH  "/sdcard/Music"
#define DEFAULT_ODK_PATH    "/sdcard/.opendeck"

// Queue for tracks needing analysis
typedef struct {
    char path[256];
} prep_queue_item_t;

// Static state
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_initialized = false;
static volatile bool s_running = false;
static volatile bool s_cancel = false;
static volatile track_prep_status_t s_status = TRACK_PREP_STATUS_IDLE;
static volatile int s_progress = 0;
static char s_phase[64] = {0};
static char s_scan_path[256] = {0};
static track_prep_mode_t s_mode = TRACK_PREP_MODE_QUICK;

// Statistics
static track_prep_stats_t s_stats = {0};

// Issues array
static track_issue_t *s_issues = NULL;
static uint32_t s_issue_count = 0;

// Tracks to analyze
static prep_queue_item_t *s_queue = NULL;
static uint32_t s_queue_count = 0;

// Callback
static track_prep_progress_cb s_callback = NULL;
static void *s_callback_data = NULL;

// Timing
static int64_t s_start_time = 0;
static int64_t s_last_analysis_time = 0;

// Forward declarations
static void prep_task_func(void *arg);
static void scan_music_directory(const char *path);
static void scan_odk_directory(const char *path);
static void analyze_queued_tracks(void);
static void verify_odk_files(void);
static void add_issue(const char *path, track_issue_type_t type, 
                      uint32_t source_size, uint32_t odk_size);
static void report_progress(void);
static void set_phase(const char *phase);

bool track_prep_init(void) {
    if (s_initialized) {
        return true;
    }
    
    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Allocate issues array in PSRAM
    s_issues = heap_caps_calloc(TRACK_PREP_MAX_ISSUES, sizeof(track_issue_t),
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_issues) {
        ESP_LOGE(TAG, "Failed to allocate issues array");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return false;
    }
    
    // Allocate queue in PSRAM
    s_queue = heap_caps_calloc(TRACK_PREP_MAX_QUEUE, sizeof(prep_queue_item_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to allocate queue");
        heap_caps_free(s_issues);
        s_issues = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return false;
    }
    
    // Ensure analysis task is initialized
    if (!analysis_task_init()) {
        ESP_LOGW(TAG, "Analysis task not available");
        // Continue anyway - scanning still works
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Track preparation system initialized");
    return true;
}

bool track_prep_start(track_prep_mode_t mode, const char *path) {
    if (!s_initialized) {
        if (!track_prep_init()) {
            return false;
        }
    }
    
    if (s_running) {
        ESP_LOGW(TAG, "Preparation already running");
        return false;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    // Reset state
    s_mode = mode;
    s_cancel = false;
    s_progress = 0;
    s_issue_count = 0;
    s_queue_count = 0;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_issues, 0, TRACK_PREP_MAX_ISSUES * sizeof(track_issue_t));
    memset(s_queue, 0, TRACK_PREP_MAX_QUEUE * sizeof(prep_queue_item_t));
    
    // Set scan path
    if (path && path[0]) {
        strncpy(s_scan_path, path, sizeof(s_scan_path) - 1);
    } else {
        strncpy(s_scan_path, DEFAULT_MUSIC_PATH, sizeof(s_scan_path) - 1);
    }
    
    s_status = TRACK_PREP_STATUS_SCANNING;
    s_start_time = esp_timer_get_time();
    s_running = true;
    
    xSemaphoreGive(s_mutex);
    
    // Create preparation task
    BaseType_t ret = xTaskCreatePinnedToCore(
        prep_task_func,
        "track_prep",
        PREP_TASK_STACK,
        NULL,
        PREP_TASK_PRIORITY,
        &s_task,
        PREP_CORE
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create prep task");
        s_running = false;
        s_status = TRACK_PREP_STATUS_ERROR;
        return false;
    }
    
    ESP_LOGI(TAG, "Started preparation (mode=%d, path=%s)", mode, s_scan_path);
    return true;
}

bool track_prep_start_playlist(const char *playlist_path) {
    // TODO: Parse playlist and queue only those tracks
    // For now, just start with the music directory
    ESP_LOGW(TAG, "Playlist preparation not yet implemented, using full scan");
    return track_prep_start(TRACK_PREP_MODE_FULL, NULL);
}

void track_prep_cancel(void) {
    if (!s_running) return;
    
    s_cancel = true;
    analysis_task_cancel();
    
    ESP_LOGI(TAG, "Cancellation requested");
}

bool track_prep_is_running(void) {
    return s_running;
}

track_prep_status_t track_prep_get_status(void) {
    return s_status;
}

int track_prep_get_progress(void) {
    return s_progress;
}

void track_prep_get_stats(track_prep_stats_t *stats) {
    if (!stats) return;
    
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100));
    memcpy(stats, &s_stats, sizeof(track_prep_stats_t));
    xSemaphoreGive(s_mutex);
}

const char* track_prep_get_phase(void) {
    return s_phase;
}

uint32_t track_prep_get_issue_count(void) {
    return s_issue_count;
}

bool track_prep_get_issue(uint32_t index, track_issue_t *issue) {
    if (!issue || index >= s_issue_count) {
        return false;
    }
    
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100));
    memcpy(issue, &s_issues[index], sizeof(track_issue_t));
    xSemaphoreGive(s_mutex);
    
    return true;
}

void track_prep_set_callback(track_prep_progress_cb cb, void *user_data) {
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100));
    s_callback = cb;
    s_callback_data = user_data;
    xSemaphoreGive(s_mutex);
}

bool track_prep_resolve_issue(uint32_t index) {
    if (index >= s_issue_count) {
        return false;
    }
    
    track_issue_t *issue = &s_issues[index];
    
    switch (issue->type) {
        case TRACK_ISSUE_MISSING_ODK:
        case TRACK_ISSUE_SIZE_MISMATCH:
        case TRACK_ISSUE_ANALYSIS_FAILED:
            // Queue for analysis
            return analysis_task_queue(issue->path);
            
        case TRACK_ISSUE_ORPHAN_ODK: {
            // Delete orphaned file
            char odk_path[280];
            if (metadata_get_odk_path(issue->path, odk_path, sizeof(odk_path))) {
                if (unlink(odk_path) == 0) {
                    ESP_LOGI(TAG, "Deleted orphan: %s", odk_path);
                    return true;
                }
            }
            // Try direct path if it's already an odk path
            if (unlink(issue->path) == 0) {
                ESP_LOGI(TAG, "Deleted orphan: %s", issue->path);
                return true;
            }
            return false;
        }
        
        case TRACK_ISSUE_CORRUPT_ODK: {
            // Delete corrupt file and re-analyze
            char odk_path[280];
            if (metadata_get_odk_path(issue->path, odk_path, sizeof(odk_path))) {
                unlink(odk_path);
            }
            return analysis_task_queue(issue->path);
        }
        
        default:
            return false;
    }
}

uint32_t track_prep_resolve_all(track_issue_type_t type) {
    uint32_t resolved = 0;
    
    for (uint32_t i = 0; i < s_issue_count; i++) {
        if (s_issues[i].type == type) {
            if (track_prep_resolve_issue(i)) {
                resolved++;
            }
        }
    }
    
    return resolved;
}

bool track_prep_quick_scan(const char *path, track_prep_stats_t *stats) {
    if (!stats) return false;
    
    if (!s_initialized && !track_prep_init()) {
        return false;
    }
    
    if (s_running) {
        ESP_LOGW(TAG, "Cannot quick scan while preparation is running");
        return false;
    }
    
    // Use stored stats if recent
    if (s_stats.total_mp3s > 0) {
        memcpy(stats, &s_stats, sizeof(track_prep_stats_t));
        return true;
    }
    
    // Start quick scan
    return track_prep_start(TRACK_PREP_MODE_QUICK, path);
}

bool track_prep_needs_analysis(const char *mp3_path) {
    if (!mp3_path) return false;
    
    // Check if .odk exists
    if (!metadata_exists(mp3_path)) {
        return true;
    }
    
    // Check file size hasn't changed
    struct stat st;
    if (stat(mp3_path, &st) != 0) {
        return false;  // Source doesn't exist
    }
    
    TrackMetadata_t meta;
    if (!metadata_load(mp3_path, &meta)) {
        return true;  // Corrupt metadata
    }
    
    if (meta.source_size != (uint32_t)st.st_size) {
        return true;  // Size changed
    }
    
    return false;
}

uint32_t track_prep_get_eta(void) {
    if (!s_running || s_stats.needs_analysis == 0) {
        return 0;
    }
    
    if (s_stats.analyzed_now == 0 || s_last_analysis_time == 0) {
        // Estimate ~30 seconds per track
        return (s_stats.needs_analysis - s_stats.analyzed_now) * 30;
    }
    
    // Calculate based on actual performance
    int64_t elapsed = esp_timer_get_time() - s_start_time;
    if (elapsed <= 0 || s_stats.analyzed_now == 0) {
        return 0;
    }
    
    // Average time per track in microseconds
    int64_t avg_time = elapsed / s_stats.analyzed_now;
    uint32_t remaining = s_stats.needs_analysis - s_stats.analyzed_now;
    
    // Return seconds
    return (uint32_t)((avg_time * remaining) / 1000000);
}

uint32_t track_prep_cleanup_orphans(bool dry_run) {
    uint32_t count = 0;
    
    // This would scan .opendeck directory and check each .odk
    // For now, just return the count from issues
    for (uint32_t i = 0; i < s_issue_count; i++) {
        if (s_issues[i].type == TRACK_ISSUE_ORPHAN_ODK) {
            if (!dry_run) {
                track_prep_resolve_issue(i);
            }
            count++;
        }
    }
    
    return count;
}

// ============================================================================
// Internal implementation
// ============================================================================

static void set_phase(const char *phase) {
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50));
    strncpy(s_phase, phase, sizeof(s_phase) - 1);
    xSemaphoreGive(s_mutex);
}

static void report_progress(void) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_callback) {
            s_callback(s_status, s_phase, 
                      s_stats.analyzed_now, 
                      s_stats.needs_analysis,
                      s_progress, s_callback_data);
        }
        xSemaphoreGive(s_mutex);
    }
}

static void add_issue(const char *path, track_issue_type_t type,
                      uint32_t source_size, uint32_t odk_size) {
    if (s_issue_count >= TRACK_PREP_MAX_ISSUES) {
        return;
    }
    
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100));
    
    track_issue_t *issue = &s_issues[s_issue_count];
    strncpy(issue->path, path, sizeof(issue->path) - 1);
    issue->type = type;
    issue->source_size = source_size;
    issue->odk_size = odk_size;
    
    s_issue_count++;
    s_stats.issues_found++;
    
    xSemaphoreGive(s_mutex);
}

/**
 * @brief Recursively scan music directory for MP3 files
 */
static void scan_music_recursive(const char *path, uint32_t *mp3_count, 
                                  uint32_t *analyzed_count, uint32_t *needs_count) {
    if (s_cancel) return;
    
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open directory: %s", path);
        return;
    }
    
    struct dirent *entry;
    char filepath[512];
    
    while ((entry = readdir(dir)) != NULL && !s_cancel) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') continue;
        
        // Build full path
        int ret = snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        if (ret >= (int)sizeof(filepath)) continue;
        
        struct stat st;
        if (stat(filepath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            scan_music_recursive(filepath, mp3_count, analyzed_count, needs_count);
        } else if (S_ISREG(st.st_mode)) {
            // Check if MP3 file
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcasecmp(entry->d_name + len - 4, ".mp3") == 0) {
                (*mp3_count)++;
                
                // Check if .odk exists
                char odk_path[280];
                if (metadata_get_odk_path(filepath, odk_path, sizeof(odk_path))) {
                    struct stat odk_st;
                    if (stat(odk_path, &odk_st) == 0) {
                        // .odk exists - check if valid
                        TrackMetadata_t meta;
                        if (metadata_load(filepath, &meta)) {
                            // Check source size
                            if (meta.source_size == (uint32_t)st.st_size) {
                                (*analyzed_count)++;
                            } else {
                                // Size mismatch
                                (*needs_count)++;
                                add_issue(filepath, TRACK_ISSUE_SIZE_MISMATCH,
                                         (uint32_t)st.st_size, meta.source_size);
                                s_stats.size_mismatches++;
                                
                                // Queue for re-analysis
                                if (s_queue_count < TRACK_PREP_MAX_QUEUE) {
                                    strncpy(s_queue[s_queue_count].path, filepath,
                                            sizeof(s_queue[s_queue_count].path) - 1);
                                    s_queue_count++;
                                }
                            }
                        } else {
                            // Corrupt .odk
                            (*needs_count)++;
                            add_issue(filepath, TRACK_ISSUE_CORRUPT_ODK, 0, 0);
                            
                            if (s_queue_count < TRACK_PREP_MAX_QUEUE) {
                                strncpy(s_queue[s_queue_count].path, filepath,
                                        sizeof(s_queue[s_queue_count].path) - 1);
                                s_queue_count++;
                            }
                        }
                    } else {
                        // Missing .odk
                        (*needs_count)++;
                        add_issue(filepath, TRACK_ISSUE_MISSING_ODK, 
                                 (uint32_t)st.st_size, 0);
                        
                        if (s_queue_count < TRACK_PREP_MAX_QUEUE) {
                            strncpy(s_queue[s_queue_count].path, filepath,
                                    sizeof(s_queue[s_queue_count].path) - 1);
                            s_queue_count++;
                        }
                    }
                }
                
                // Update progress periodically
                if (*mp3_count % 10 == 0) {
                    char phase[64];
                    snprintf(phase, sizeof(phase), "Scanning... %lu files", (unsigned long)*mp3_count);
                    set_phase(phase);
                    
                    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50));
                    s_stats.total_mp3s = *mp3_count;
                    s_stats.already_analyzed = *analyzed_count;
                    s_stats.needs_analysis = *needs_count;
                    xSemaphoreGive(s_mutex);
                    
                    report_progress();
                    vTaskDelay(1);  // Yield
                }
            }
        }
    }
    
    closedir(dir);
}

/**
 * @brief Scan for orphaned .odk files
 */
static void scan_odk_orphans_recursive(const char *odk_path) {
    if (s_cancel) return;
    
    DIR *dir = opendir(odk_path);
    if (!dir) return;
    
    struct dirent *entry;
    char filepath[512];
    
    while ((entry = readdir(dir)) != NULL && !s_cancel) {
        if (entry->d_name[0] == '.') continue;
        
        int ret = snprintf(filepath, sizeof(filepath), "%s/%s", odk_path, entry->d_name);
        if (ret >= (int)sizeof(filepath)) continue;
        
        struct stat st;
        if (stat(filepath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            scan_odk_orphans_recursive(filepath);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcasecmp(entry->d_name + len - 4, ".odk") == 0) {
                // Convert .odk path back to MP3 path
                // /sdcard/.opendeck/Music/Track.odk -> /sdcard/Music/Track.mp3
                char mp3_path[512];
                const char *rel_path = filepath + strlen("/sdcard/.opendeck");
                int mp3_ret = snprintf(mp3_path, sizeof(mp3_path), "/sdcard%s", rel_path);
                if (mp3_ret >= (int)sizeof(mp3_path)) continue;
                
                char *ext = strrchr(mp3_path, '.');
                if (ext) strcpy(ext, ".mp3");
                
                // Check if source MP3 exists
                struct stat mp3_st;
                if (stat(mp3_path, &mp3_st) != 0) {
                    // Source doesn't exist - orphaned .odk
                    add_issue(filepath, TRACK_ISSUE_ORPHAN_ODK, 0, 0);
                    s_stats.orphan_odks++;
                }
            }
        }
        
        vTaskDelay(1);  // Yield
    }
    
    closedir(dir);
}

static void scan_music_directory(const char *path) {
    set_phase("Scanning music library...");
    s_status = TRACK_PREP_STATUS_SCANNING;
    report_progress();
    
    uint32_t mp3_count = 0;
    uint32_t analyzed_count = 0;
    uint32_t needs_count = 0;
    
    scan_music_recursive(path, &mp3_count, &analyzed_count, &needs_count);
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_stats.total_mp3s = mp3_count;
    s_stats.already_analyzed = analyzed_count;
    s_stats.needs_analysis = needs_count;
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Scan complete: %u MP3s, %u analyzed, %u need analysis",
             mp3_count, analyzed_count, needs_count);
}

static void scan_odk_directory(const char *path) {
    set_phase("Checking for orphaned files...");
    report_progress();
    
    scan_odk_orphans_recursive(path);
    
    ESP_LOGI(TAG, "Found %u orphaned .odk files", s_stats.orphan_odks);
}

/**
 * @brief Analysis progress callback from analysis_task
 */
static void analysis_callback(const char *filename, int percent, void *user_data) {
    (void)user_data;
    
    // Update phase with current file
    char phase[128];
    snprintf(phase, sizeof(phase), "Analyzing: %s (%d%%)", filename, percent);
    set_phase(phase);
    
    // Update overall progress
    if (s_stats.needs_analysis > 0) {
        int base_progress = 20;  // Scanning took 0-20%
        int analysis_range = 80; // Analysis is 20-100%
        
        int file_progress = (s_stats.analyzed_now * 100) / s_stats.needs_analysis;
        int current_file_contrib = percent / s_stats.needs_analysis;
        
        s_progress = base_progress + ((file_progress + current_file_contrib) * analysis_range / 100);
    }
    
    report_progress();
}

static void analyze_queued_tracks(void) {
    if (s_queue_count == 0) {
        ESP_LOGI(TAG, "No tracks to analyze");
        s_progress = 100;
        return;
    }
    
    s_status = TRACK_PREP_STATUS_ANALYZING;
    set_phase("Starting analysis...");
    s_progress = 20;  // Scanning complete = 20%
    report_progress();
    
    // Set up analysis callback
    analysis_task_set_callback(analysis_callback, NULL);
    
    // Queue all tracks for analysis
    ESP_LOGI(TAG, "Queueing %u tracks for analysis", s_queue_count);
    
    for (uint32_t i = 0; i < s_queue_count && !s_cancel; i++) {
        if (!analysis_task_queue(s_queue[i].path)) {
            ESP_LOGW(TAG, "Failed to queue: %s", s_queue[i].path);
        }
    }
    
    // Wait for analysis to complete
    while (!s_cancel && 
           (analysis_task_is_busy() || analysis_task_get_queue_depth() > 0)) {
        
        // Update stats
        xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50));
        
        // Count completed analyses
        uint32_t completed = 0;
        for (uint32_t i = 0; i < s_queue_count; i++) {
            if (metadata_exists(s_queue[i].path)) {
                completed++;
            }
        }
        s_stats.analyzed_now = completed;
        
        xSemaphoreGive(s_mutex);
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Clear callback
    analysis_task_set_callback(NULL, NULL);
    
    // Final stats
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50));
    uint32_t final_completed = 0;
    for (uint32_t i = 0; i < s_queue_count; i++) {
        if (metadata_exists(s_queue[i].path)) {
            final_completed++;
        } else {
            s_stats.failed++;
        }
    }
    s_stats.analyzed_now = final_completed;
    xSemaphoreGive(s_mutex);
    
    s_progress = 100;
    
    ESP_LOGI(TAG, "Analysis complete: %u succeeded, %u failed",
             s_stats.analyzed_now, s_stats.failed);
}

static void verify_odk_files(void) {
    s_status = TRACK_PREP_STATUS_VERIFYING;
    set_phase("Verifying metadata files...");
    report_progress();
    
    // Verification is done during the music scan
    // This phase can do additional checks if needed
    
    ESP_LOGI(TAG, "Verification complete");
}

/**
 * @brief Main preparation task
 */
static void prep_task_func(void *arg) {
    (void)arg;
    
    ESP_LOGI(TAG, "Preparation task started (mode=%d)", s_mode);
    
    // Phase 1: Scan music directory
    scan_music_directory(s_scan_path);
    s_progress = 10;
    report_progress();
    
    if (s_cancel) {
        s_status = TRACK_PREP_STATUS_CANCELLED;
        goto cleanup;
    }
    
    // Phase 2: Check for orphaned .odk files (if full or verify mode)
    if (s_mode == TRACK_PREP_MODE_FULL || s_mode == TRACK_PREP_MODE_VERIFY) {
        scan_odk_directory(DEFAULT_ODK_PATH);
    }
    s_progress = 20;
    report_progress();
    
    if (s_cancel) {
        s_status = TRACK_PREP_STATUS_CANCELLED;
        goto cleanup;
    }
    
    // Phase 3: Analyze tracks (if not quick mode)
    if (s_mode == TRACK_PREP_MODE_FULL || s_mode == TRACK_PREP_MODE_PLAYLIST) {
        analyze_queued_tracks();
    }
    
    if (s_cancel) {
        s_status = TRACK_PREP_STATUS_CANCELLED;
        goto cleanup;
    }
    
    // Phase 4: Final verification (if verify mode)
    if (s_mode == TRACK_PREP_MODE_VERIFY) {
        verify_odk_files();
    }
    
    s_status = TRACK_PREP_STATUS_COMPLETE;
    s_progress = 100;
    set_phase("Preparation complete");
    report_progress();
    
cleanup:
    ESP_LOGI(TAG, "Preparation finished (status=%d)", s_status);
    
    // Log summary
    ESP_LOGI(TAG, "Summary: %u total, %u analyzed, %u new, %u failed, %u issues",
             s_stats.total_mp3s, s_stats.already_analyzed,
             s_stats.analyzed_now, s_stats.failed, s_stats.issues_found);
    
    s_running = false;
    s_task = NULL;
    vTaskDelete(NULL);
}
