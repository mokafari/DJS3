/**
 * @file analysis_task.c
 * @brief Background track analysis task implementation
 * 
 * Runs BPM detection, waveform generation, and key detection
 * on queued tracks. Results are saved to .odk metadata files.
 */

#include "analysis_task.h"
#include "bpm_detector.h"
#include "waveform_gen.h"
#include "key_detector.h"
#include "metadata.h"
#include "metadata_format.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

static const char *TAG = "analysis";

// Task configuration
#define ANALYSIS_QUEUE_SIZE     50
#define ANALYSIS_TASK_STACK     16384   // Needs stack for MP3 decoding
#define ANALYSIS_TASK_PRIORITY  2       // Low priority, background work
#define ANALYSIS_CORE           0       // Run on Core 0 (audio playback on Core 1)

// Analysis chunk size (samples per decode iteration)
#define DECODE_CHUNK_SAMPLES    4096

// Maximum duration to analyze for BPM (seconds)
#define BPM_ANALYSIS_DURATION   30

// Queue item
typedef struct {
    char path[256];
} analysis_item_t;

// Static variables
static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;

static volatile bool s_initialized = false;
static volatile bool s_paused = false;
static volatile bool s_cancel_current = false;
static volatile bool s_busy = false;
static volatile int s_progress = 0;
static char s_current_file[256] = {0};
static analysis_progress_cb s_callback = NULL;
static void *s_callback_data = NULL;

// Forward declarations
static void analysis_task_func(void *arg);
static bool analyze_track(const char *mp3_path);
static void report_progress(int percent);

bool analysis_task_init(void) {
    if (s_initialized) {
        return true;
    }
    
    // Create queue
    s_queue = xQueueCreate(ANALYSIS_QUEUE_SIZE, sizeof(analysis_item_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return false;
    }
    
    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    
    // Create task pinned to Core 0
    BaseType_t ret = xTaskCreatePinnedToCore(
        analysis_task_func,
        "analysis",
        ANALYSIS_TASK_STACK,
        NULL,
        ANALYSIS_TASK_PRIORITY,
        &s_task,
        ANALYSIS_CORE
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_mutex);
        s_queue = NULL;
        s_mutex = NULL;
        return false;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Analysis task initialized on Core %d", ANALYSIS_CORE);
    return true;
}

bool analysis_task_queue(const char *mp3_path) {
    if (!s_initialized || !mp3_path) {
        return false;
    }
    
    // Check if .odk already exists
    char odk_path[280];
    if (metadata_get_odk_path(mp3_path, odk_path, sizeof(odk_path))) {
        struct stat st;
        if (stat(odk_path, &st) == 0) {
            ESP_LOGD(TAG, "Already analyzed: %s", mp3_path);
            return true;  // Already done, return success
        }
    }
    
    // Check if already in queue or currently processing
    if (s_busy && strcmp(s_current_file, mp3_path) == 0) {
        ESP_LOGD(TAG, "Already processing: %s", mp3_path);
        return true;
    }
    
    // Add to queue
    analysis_item_t item;
    memset(&item, 0, sizeof(item));
    strncpy(item.path, mp3_path, sizeof(item.path) - 1);
    
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full, cannot add: %s", mp3_path);
        return false;
    }
    
    ESP_LOGI(TAG, "Queued for analysis: %s (depth: %d)", 
             mp3_path, (int)uxQueueMessagesWaiting(s_queue));
    return true;
}

int analysis_task_queue_batch(const char **paths, int count) {
    int queued = 0;
    for (int i = 0; i < count; i++) {
        if (analysis_task_queue(paths[i])) {
            queued++;
        }
    }
    return queued;
}

int analysis_task_get_queue_depth(void) {
    if (!s_queue) {
        return 0;
    }
    return (int)uxQueueMessagesWaiting(s_queue);
}

bool analysis_task_is_busy(void) {
    return s_busy;
}

int analysis_task_get_progress(void) {
    return s_progress;
}

const char* analysis_task_get_current_file(void) {
    if (!s_busy) {
        return NULL;
    }
    return s_current_file;
}

void analysis_task_set_callback(analysis_progress_cb cb, void *user_data) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_callback = cb;
        s_callback_data = user_data;
        xSemaphoreGive(s_mutex);
    }
}

void analysis_task_cancel(void) {
    s_cancel_current = true;
    xQueueReset(s_queue);
    ESP_LOGI(TAG, "Analysis cancelled and queue cleared");
}

void analysis_task_pause(void) {
    s_paused = true;
    ESP_LOGI(TAG, "Analysis paused");
}

void analysis_task_resume(void) {
    s_paused = false;
    ESP_LOGI(TAG, "Analysis resumed");
}

bool analysis_task_is_paused(void) {
    return s_paused;
}

static void report_progress(int percent) {
    s_progress = percent;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_callback) {
            // Extract just filename for callback
            const char *filename = strrchr(s_current_file, '/');
            filename = filename ? filename + 1 : s_current_file;
            s_callback(filename, percent, s_callback_data);
        }
        xSemaphoreGive(s_mutex);
    }
}

/**
 * @brief Main analysis task function
 */
static void analysis_task_func(void *arg) {
    (void)arg;
    analysis_item_t item;
    
    ESP_LOGI(TAG, "Analysis task started");
    
    while (1) {
        // Wait for item in queue (1 second timeout)
        if (xQueueReceive(s_queue, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            // Wait while paused
            while (s_paused && !s_cancel_current) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // Skip if cancelled during pause
            if (s_cancel_current) {
                s_cancel_current = false;
                continue;
            }
            
            // Set up state for this analysis
            if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
                strncpy(s_current_file, item.path, sizeof(s_current_file) - 1);
                s_busy = true;
                s_progress = 0;
                s_cancel_current = false;
                xSemaphoreGive(s_mutex);
            }
            
            ESP_LOGI(TAG, "Starting analysis: %s", item.path);
            
            bool success = analyze_track(item.path);
            
            if (success) {
                ESP_LOGI(TAG, "Analysis complete: %s", item.path);
            } else if (s_cancel_current) {
                ESP_LOGI(TAG, "Analysis cancelled: %s", item.path);
            } else {
                ESP_LOGW(TAG, "Analysis failed: %s", item.path);
            }
            
            // Clear state
            s_busy = false;
            s_progress = success ? 100 : 0;
            s_current_file[0] = '\0';
            s_cancel_current = false;
        }
        
        // Small delay when idle to avoid tight loop
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Analyze a single track
 * 
 * This is a skeleton implementation - full version needs:
 * 1. MP3 decoder to extract PCM samples
 * 2. Streaming to BPM detector and waveform generator
 * 3. Key detection (optional, CPU intensive)
 * 
 * @param mp3_path  Path to MP3 file
 * @return true on success
 */
static bool analyze_track(const char *mp3_path) {
    // Initialize metadata structure
    TrackMetadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.magic = ODK_MAGIC;
    metadata.version = ODK_VERSION;
    
    // Get source file info
    struct stat st;
    if (stat(mp3_path, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat file: %s", mp3_path);
        return false;
    }
    metadata.source_size = (uint32_t)st.st_size;
    
    report_progress(5);
    
    // Check for cancellation
    if (s_cancel_current) {
        return false;
    }
    
    // =========================================================================
    // TODO: Actual MP3 decoding and analysis
    // 
    // The full implementation would:
    // 1. Open MP3 file using libhelix-mp3
    // 2. Create BPM detector context: bpm_detector_create(44100, BPM_ANALYSIS_DURATION)
    // 3. Create waveform generator: waveform_gen_create(total_samples)
    // 4. Decode MP3 in chunks:
    //    - Feed samples to bpm_detector_feed()
    //    - Feed samples to waveform_gen_feed()
    //    - Update progress
    // 5. Finalize: bpm_detector_finish(), waveform_gen_finish()
    // 6. Optionally run key_detect() on accumulated samples
    // =========================================================================
    
    report_progress(10);
    
    // For now, generate placeholder data
    // This allows the task structure to be tested while MP3 decoding is implemented
    
    // Placeholder BPM (will be replaced with actual detection)
    metadata.bpm = 0.0f;  // 0 = not detected
    
    // Placeholder duration - should come from MP3 header/decoding
    // Estimate based on file size (~128kbps assumption)
    uint32_t estimated_duration_ms = (uint32_t)(((uint64_t)metadata.source_size * 8) / 128);
    metadata.duration_ms = estimated_duration_ms;
    
    report_progress(30);
    
    if (s_cancel_current) {
        return false;
    }
    
    // Placeholder key (unknown)
    metadata.key_id = 255;
    metadata.grid_offset = 0;
    
    report_progress(50);
    
    // Generate placeholder waveform (flat line)
    // Real implementation: waveform_gen_finish(ctx, metadata.waveform_overview)
    memset(metadata.waveform_overview, 64, WAVEFORM_POINTS);
    
    report_progress(70);
    
    if (s_cancel_current) {
        return false;
    }
    
    // Generate linear seek table (placeholder for VBR)
    // Real implementation: build during MP3 frame scanning
    for (int i = 0; i < SEEK_POINTS; i++) {
        metadata.seek_table[i] = (metadata.source_size * (uint32_t)i) / SEEK_POINTS;
    }
    
    report_progress(85);
    
    // Initialize empty hot cues
    for (int i = 0; i < NUM_HOTCUES; i++) {
        metadata.hotcues[i].active = 0;
        metadata.hotcues[i].position_ms = 0;
        metadata.hotcues[i].color_rgb565 = 0;
        metadata.hotcues[i].is_loop = 0;
    }
    
    report_progress(90);
    
    // Save metadata to .odk file
    bool saved = metadata_save(mp3_path, &metadata);
    if (!saved) {
        ESP_LOGE(TAG, "Failed to save metadata for: %s", mp3_path);
        return false;
    }
    
    report_progress(100);
    
    // Log what we saved
    ESP_LOGI(TAG, "Saved metadata: duration=%lums, bpm=%.1f, size=%lu bytes",
             (unsigned long)metadata.duration_ms, 
             metadata.bpm,
             (unsigned long)metadata.source_size);
    
    return true;
}
