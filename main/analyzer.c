/**
 * @file analyzer.c
 * @brief Track analyzer implementation with two-pass PSRAM-safe analysis
 * 
 * Pass 1: Fast header scan - builds seek table and calculates duration
 * Pass 2: PCM decode - generates waveform and detects BPM (idle only)
 */

#include "analyzer.h"
#include "metadata.h"
#include "metadata_format.h"
#include "audio_player.h"
#include "id3_parser.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mp3dec.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "ANALYZER";

// Task configuration
#define PASS1_STACK_SIZE    4096
#define PASS2_STACK_SIZE    8192
#define PREANALYSIS_STACK   8192
#define PASS1_PRIORITY      2
#define PASS2_PRIORITY      1
#define PREANALYSIS_PRIORITY 1

// Pre-analysis queue
#define PREANALYSIS_QUEUE_SIZE 5
#define PATH_MAX_LEN 256

// State
static volatile analyzer_state_t state = ANALYZER_STATE_IDLE;
static volatile bool suspend_requested = false;
static char current_filepath[PATH_MAX_LEN] = {0};

// Tasks
static TaskHandle_t pass1_task_handle = NULL;
static TaskHandle_t pass2_task_handle = NULL;
static TaskHandle_t preanalysis_task_handle = NULL;

// Queues
static QueueHandle_t preanalysis_queue = NULL;
static QueueHandle_t pass2_queue = NULL;

// Callback
static analyzer_progress_cb_t progress_callback = NULL;

// Forward declarations
static void pass1_task(void *pvParameters);
static void pass2_task(void *pvParameters);
static void preanalysis_task(void *pvParameters);
static bool can_run_pass2(void);
static void analyze_pass1(const char *filepath);
static void analyze_pass2(const char *filepath);

/**
 * @brief Initialize analyzer subsystem
 */
void analyzer_init(void) {
    ESP_LOGI(TAG, "Initializing analyzer");
    
    // Create queues
    preanalysis_queue = xQueueCreate(PREANALYSIS_QUEUE_SIZE, PATH_MAX_LEN);
    pass2_queue = xQueueCreate(1, PATH_MAX_LEN);  // Only one pending
    
    if (!preanalysis_queue || !pass2_queue) {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }
    
    // Create pre-analysis background task
    xTaskCreate(preanalysis_task, "preanalysis", PREANALYSIS_STACK, 
                NULL, PREANALYSIS_PRIORITY, &preanalysis_task_handle);
    
    ESP_LOGI(TAG, "Analyzer initialized");
}

/**
 * @brief Check if Pass 2 can run (deck must be paused/stopped)
 */
static bool can_run_pass2(void) {
    audio_player_state_t player_state = audio_player_get_state();
    return (player_state == AUDIO_PLAYER_STATE_PAUSED || 
            player_state == AUDIO_PLAYER_STATE_STOPPED);
}

/**
 * @brief Start analysis for a track
 */
void analyzer_start(const char *filepath) {
    if (!filepath || strlen(filepath) >= PATH_MAX_LEN) {
        ESP_LOGE(TAG, "Invalid filepath");
        return;
    }
    
    // Cancel any existing analysis
    analyzer_cancel();
    
    // Store filepath
    strncpy(current_filepath, filepath, PATH_MAX_LEN - 1);
    current_filepath[PATH_MAX_LEN - 1] = '\0';
    
    // Start Pass 1 task
    char *path_copy = strdup(filepath);
    if (!path_copy) {
        ESP_LOGE(TAG, "Failed to allocate path");
        return;
    }
    
    ESP_LOGI(TAG, "Starting analysis: %s", filepath);
    
    xTaskCreate(pass1_task, "analyzer_p1", PASS1_STACK_SIZE,
                path_copy, PASS1_PRIORITY, &pass1_task_handle);
}

/**
 * @brief Suspend analyzer
 */
void analyzer_suspend(void) {
    suspend_requested = true;
    ESP_LOGD(TAG, "Suspend requested");
}

/**
 * @brief Resume analyzer
 */
void analyzer_resume(void) {
    suspend_requested = false;
    ESP_LOGD(TAG, "Resume requested");
    
    // If Pass 2 is pending, start it
    if (state == ANALYZER_STATE_PASS2_PENDING && can_run_pass2()) {
        char path[PATH_MAX_LEN];
        if (xQueueReceive(pass2_queue, path, 0) == pdTRUE) {
            char *path_copy = strdup(path);
            if (path_copy) {
                xTaskCreate(pass2_task, "analyzer_p2", PASS2_STACK_SIZE,
                            path_copy, PASS2_PRIORITY, &pass2_task_handle);
            }
        }
    }
}

/**
 * @brief Cancel analysis
 */
void analyzer_cancel(void) {
    suspend_requested = true;
    
    // Delete running tasks
    if (pass1_task_handle) {
        vTaskDelete(pass1_task_handle);
        pass1_task_handle = NULL;
    }
    if (pass2_task_handle) {
        vTaskDelete(pass2_task_handle);
        pass2_task_handle = NULL;
    }
    
    // Clear queues
    if (pass2_queue) {
        xQueueReset(pass2_queue);
    }
    
    state = ANALYZER_STATE_IDLE;
    suspend_requested = false;
    current_filepath[0] = '\0';
    
    ESP_LOGI(TAG, "Analysis cancelled");
}

/**
 * @brief Get current state
 */
analyzer_state_t analyzer_get_state(void) {
    return state;
}

/**
 * @brief Check if analysis is complete
 */
bool analyzer_is_complete(const char *filepath) {
    TrackMetadata_t meta;
    if (!metadata_load(filepath, &meta)) {
        return false;
    }
    
    // Check if waveform is populated (Pass 2 complete)
    // A valid waveform should have some non-zero values
    int nonzero = 0;
    for (int i = 0; i < WAVEFORM_POINTS; i++) {
        if (meta.waveform_overview[i] > 0) nonzero++;
    }
    
    return (meta.duration_ms > 0 && nonzero > WAVEFORM_POINTS / 10);
}

/**
 * @brief Queue track for pre-analysis
 */
void analyzer_queue_preanalysis(const char *filepath) {
    if (!preanalysis_queue || !filepath) return;
    
    // Only queue if idle
    if (state != ANALYZER_STATE_IDLE) return;
    
    // Check if already analyzed
    if (metadata_exists(filepath)) return;
    
    char path[PATH_MAX_LEN];
    strncpy(path, filepath, PATH_MAX_LEN - 1);
    path[PATH_MAX_LEN - 1] = '\0';
    
    // Non-blocking send
    if (xQueueSend(preanalysis_queue, path, 0) == pdTRUE) {
        ESP_LOGD(TAG, "Queued for pre-analysis: %s", filepath);
    }
}

/**
 * @brief Set progress callback
 */
void analyzer_set_progress_callback(analyzer_progress_cb_t callback) {
    progress_callback = callback;
}

/**
 * @brief Get queue count
 */
int analyzer_get_queue_count(void) {
    if (!preanalysis_queue) return 0;
    return uxQueueMessagesWaiting(preanalysis_queue);
}

// ============================================================================
// Pass 1: Header Scan (Safe during playback)
// ============================================================================

/**
 * @brief Pass 1 task - scans MP3 headers for seek table and duration
 */
static void pass1_task(void *pvParameters) {
    char *filepath = (char *)pvParameters;
    
    state = ANALYZER_STATE_PASS1_RUNNING;
    ESP_LOGI(TAG, "Pass 1 starting: %s", filepath);
    
    analyze_pass1(filepath);
    
    // Queue Pass 2
    if (!suspend_requested) {
        state = ANALYZER_STATE_PASS2_PENDING;
        xQueueSend(pass2_queue, filepath, 0);
        
        // If we can run immediately, start Pass 2
        if (can_run_pass2()) {
            char *path_copy = strdup(filepath);
            if (path_copy) {
                xTaskCreate(pass2_task, "analyzer_p2", PASS2_STACK_SIZE,
                            path_copy, PASS2_PRIORITY, &pass2_task_handle);
            }
        }
    } else {
        state = ANALYZER_STATE_IDLE;
    }
    
    free(filepath);
    pass1_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Analyze Pass 1 - Build seek table and calculate duration
 * 
 * Scans through MP3 file reading frame headers without decoding.
 * Builds a seek table mapping percentage positions to byte offsets.
 */
static void analyze_pass1(const char *filepath) {
    TrackMetadata_t meta = {0};
    meta.magic = ODK_MAGIC;
    meta.version = ODK_VERSION;
    
    // Get source file size
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat file: %s", filepath);
        return;
    }
    meta.source_size = st.st_size;
    
    // Skip ID3 tag
    id3_tag_t id3;
    uint32_t audio_start = 0;
    if (id3_parse_file(filepath, &id3) && id3.has_tag) {
        audio_start = id3.tag_size;
    }
    
    // Validate audio size to prevent division by zero
    if (audio_start >= meta.source_size) {
        ESP_LOGE(TAG, "Invalid file: ID3 tag (%u) >= file size (%u)", 
                 audio_start, meta.source_size);
        return;
    }
    uint32_t audio_size = meta.source_size - audio_start;
    if (audio_size == 0) {
        ESP_LOGE(TAG, "Invalid file: no audio data");
        return;
    }
    
    // Open file
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", filepath);
        return;
    }
    
    fseek(f, audio_start, SEEK_SET);
    
    // Allocate small read buffer
    uint8_t *buffer = malloc(4096);
    if (!buffer) {
        fclose(f);
        ESP_LOGE(TAG, "Cannot allocate buffer");
        return;
    }
    
    // Initialize MP3 decoder for header parsing
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        free(buffer);
        fclose(f);
        ESP_LOGE(TAG, "Cannot init decoder");
        return;
    }
    
    // Scan through file
    uint32_t total_frames = 0;
    uint32_t total_samples = 0;
    int sample_rate = 44100;
    uint32_t current_pos = audio_start;
    int seek_index = 0;
    uint32_t last_seek_pos = 0;
    
    // Build seek table as we scan
    meta.seek_table[0] = audio_start;  // 0% = start of audio
    
    while (!feof(f) && !suspend_requested) {
        // Read chunk
        size_t bytes_read = fread(buffer, 1, 4096, f);
        if (bytes_read == 0) break;
        
        uint8_t *ptr = buffer;
        int remaining = bytes_read;
        
        while (remaining >= 4) {
            // Find sync word
            int offset = MP3FindSyncWord(ptr, remaining);
            if (offset < 0) break;
            
            ptr += offset;
            remaining -= offset;
            current_pos += offset;
            
            if (remaining < 4) break;
            
            // Parse frame header
            MP3FrameInfo info;
            int result = MP3GetNextFrameInfo(decoder, &info, ptr);
            
            if (result == 0 && info.nChans > 0 && info.samprate > 0) {
                // Valid frame
                total_frames++;
                total_samples += info.outputSamps / info.nChans;
                sample_rate = info.samprate;
                
                // Calculate progress
                float progress = (float)(current_pos - audio_start) / audio_size;
                int percent = (int)(progress * 100);
                
                // Update seek table
                while (seek_index < SEEK_POINTS && 
                       percent >= seek_index && 
                       current_pos > last_seek_pos) {
                    meta.seek_table[seek_index] = current_pos;
                    last_seek_pos = current_pos;
                    seek_index++;
                }
                
                // Skip to next frame
                int frame_size = info.nChans * info.outputSamps / 2 + 4;
                if (frame_size < 24) frame_size = 417;  // Default for 128kbps
                
                ptr += frame_size;
                remaining -= frame_size;
                current_pos += frame_size;
            } else {
                // Skip invalid byte
                ptr++;
                remaining--;
                current_pos++;
            }
        }
        
        // Seek back if we have remaining partial data
        if (remaining > 0) {
            fseek(f, -(long)remaining, SEEK_CUR);
        }
        
        // Report progress
        if (progress_callback && total_frames % 100 == 0) {
            int pct = (int)((float)(current_pos - audio_start) / audio_size * 100);
            progress_callback(filepath, pct, 1);
        }
    }
    
    // Fill remaining seek table entries
    while (seek_index < SEEK_POINTS) {
        meta.seek_table[seek_index] = current_pos;
        seek_index++;
    }
    
    // Calculate duration
    if (sample_rate > 0 && total_samples > 0) {
        meta.duration_ms = (uint32_t)((uint64_t)total_samples * 1000 / sample_rate);
    }
    
    // Default BPM (will be updated in Pass 2)
    meta.bpm = 120.0f;
    meta.key_id = 0;
    meta.grid_offset = 0;
    
    ESP_LOGI(TAG, "Pass 1 complete: %u frames, %u ms, %d Hz", 
             total_frames, meta.duration_ms, sample_rate);
    
    // Save metadata
    metadata_save(filepath, &meta);
    
    // Cleanup
    MP3FreeDecoder(decoder);
    free(buffer);
    fclose(f);
}

// ============================================================================
// Pass 2: PCM Decode (ONLY when idle)
// ============================================================================

/**
 * @brief Pass 2 task - decodes PCM for waveform and BPM
 */
static void pass2_task(void *pvParameters) {
    char *filepath = (char *)pvParameters;
    
    // Wait until playback is stopped
    while (!can_run_pass2()) {
        if (suspend_requested) {
            // Put back in queue for later
            xQueueSend(pass2_queue, filepath, 0);
            state = ANALYZER_STATE_PASS2_PENDING;
            free(filepath);
            pass2_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    state = ANALYZER_STATE_PASS2_RUNNING;
    ESP_LOGI(TAG, "Pass 2 starting: %s", filepath);
    
    analyze_pass2(filepath);
    
    state = ANALYZER_STATE_IDLE;
    
    free(filepath);
    pass2_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Analyze Pass 2 - Generate waveform and detect BPM
 * 
 * Decodes every Nth frame to generate a low-resolution waveform.
 * Optionally detects BPM using energy-based beat detection.
 */
static void analyze_pass2(const char *filepath) {
    // Load existing metadata from Pass 1
    TrackMetadata_t meta;
    if (!metadata_load(filepath, &meta)) {
        ESP_LOGE(TAG, "Cannot load metadata for Pass 2");
        return;
    }
    
    // Open file
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", filepath);
        return;
    }
    
    // Get audio start offset
    uint32_t audio_start = meta.seek_table[0];
    fseek(f, audio_start, SEEK_SET);
    
    // Allocate buffers
    uint8_t *read_buffer = malloc(4096);
    int16_t *decode_buffer = heap_caps_malloc(2304 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    
    if (!read_buffer || !decode_buffer) {
        if (read_buffer) free(read_buffer);
        if (decode_buffer) heap_caps_free(decode_buffer);
        fclose(f);
        ESP_LOGE(TAG, "Cannot allocate buffers");
        return;
    }
    
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        free(read_buffer);
        heap_caps_free(decode_buffer);
        fclose(f);
        return;
    }
    
    // Clear waveform
    memset(meta.waveform_overview, 0, WAVEFORM_POINTS);
    
    // Calculate audio size for progress tracking
    // We want 480 points across the entire track
    // Validate to prevent division by zero
    if (audio_start >= meta.source_size) {
        ESP_LOGE(TAG, "Invalid metadata: audio_start (%u) >= source_size (%u)", 
                 audio_start, meta.source_size);
        free(read_buffer);
        heap_caps_free(decode_buffer);
        MP3FreeDecoder(decoder);
        fclose(f);
        return;
    }
    uint32_t audio_size = meta.source_size - audio_start;
    if (audio_size == 0) {
        ESP_LOGE(TAG, "Invalid metadata: no audio data");
        free(read_buffer);
        heap_caps_free(decode_buffer);
        MP3FreeDecoder(decoder);
        fclose(f);
        return;
    }
    
    int wave_index = 0;
    uint32_t bytes_processed = 0;
    int frames_decoded = 0;
    int read_pos = 0;
    int bytes_left = 0;
    bool analysis_complete = false;  // Track if we finished naturally
    
    // For energy-based BPM detection
    float prev_energy = 0;
    int beat_count = 0;
    uint32_t first_beat_ms = 0;
    uint32_t last_beat_ms = 0;
    
    while (!feof(f) && wave_index < WAVEFORM_POINTS && !suspend_requested) {
        // Check if suspended or playback resumed
        if (suspend_requested || !can_run_pass2()) {
            ESP_LOGI(TAG, "Pass 2 interrupted (suspend=%d, can_run=%d)", 
                     suspend_requested, can_run_pass2());
            break;
        }
        
        // Read more data if needed
        if (bytes_left < 1024) {
            if (bytes_left > 0) {
                memmove(read_buffer, read_buffer + read_pos, bytes_left);
            }
            size_t read = fread(read_buffer + bytes_left, 1, 4096 - bytes_left, f);
            bytes_left += read;
            read_pos = 0;
            
            if (bytes_left == 0) break;
        }
        
        // Find sync and decode frame
        int offset = MP3FindSyncWord(read_buffer + read_pos, bytes_left);
        if (offset < 0) {
            bytes_left = 0;
            continue;
        }
        
        read_pos += offset;
        bytes_left -= offset;
        bytes_processed += offset;
        
        uint8_t *ptr = read_buffer + read_pos;
        int remaining = bytes_left;
        
        int result = MP3Decode(decoder, &ptr, &remaining, decode_buffer, 0);
        
        if (result == 0) {
            MP3FrameInfo info;
            MP3GetLastFrameInfo(decoder, &info);
            
            int consumed = bytes_left - remaining;
            read_pos += consumed;
            bytes_left = remaining;
            bytes_processed += consumed;
            
            // Find peak amplitude for this frame
            int16_t peak = 0;
            for (int i = 0; i < info.outputSamps; i++) {
                int16_t sample = abs(decode_buffer[i]);
                if (sample > peak) peak = sample;
            }
            
            // Calculate which waveform index we're at
            int new_wave_index = (int)((uint64_t)bytes_processed * WAVEFORM_POINTS / audio_size);
            if (new_wave_index >= WAVEFORM_POINTS) new_wave_index = WAVEFORM_POINTS - 1;
            
            // Update waveform peak
            uint8_t peak_byte = peak >> 7;  // Scale 0-32767 to 0-255
            if (peak_byte > meta.waveform_overview[new_wave_index]) {
                meta.waveform_overview[new_wave_index] = peak_byte;
            }
            
            wave_index = new_wave_index;
            frames_decoded++;
            
            // Simple energy-based beat detection (low frequency energy)
            float energy = 0;
            for (int i = 0; i < info.outputSamps; i++) {
                energy += (float)decode_buffer[i] * decode_buffer[i];
            }
            energy /= info.outputSamps;
            
            // Detect beat (energy spike)
            if (energy > prev_energy * 1.5f && energy > 1000000) {
                uint32_t current_ms = (uint32_t)((uint64_t)bytes_processed * meta.duration_ms / audio_size);
                
                if (beat_count == 0) {
                    first_beat_ms = current_ms;
                }
                last_beat_ms = current_ms;
                beat_count++;
            }
            prev_energy = energy * 0.9f + prev_energy * 0.1f;  // Smoothing
        } else {
            // Skip byte on error
            read_pos++;
            bytes_left--;
            bytes_processed++;
        }
        
        // Report progress periodically
        if (progress_callback && frames_decoded % 50 == 0) {
            int pct = wave_index * 100 / WAVEFORM_POINTS;
            progress_callback(filepath, pct, 2);
        }
        
        // Yield to other tasks
        if (frames_decoded % 20 == 0) {
            vTaskDelay(1);
        }
    }
    
    // Check if we completed naturally (reached end of file or waveform)
    // Only set complete if we weren't interrupted
    if (!suspend_requested && can_run_pass2() && (feof(f) || wave_index >= WAVEFORM_POINTS - 1)) {
        analysis_complete = true;
    }
    
    // Calculate BPM from beat detection (only if we got enough data)
    if (analysis_complete && beat_count > 10 && last_beat_ms > first_beat_ms) {
        float duration_sec = (last_beat_ms - first_beat_ms) / 1000.0f;
        float detected_bpm = (beat_count - 1) * 60.0f / duration_sec;
        
        // Sanity check: BPM should be 60-200
        if (detected_bpm >= 60 && detected_bpm <= 200) {
            meta.bpm = detected_bpm;
            ESP_LOGI(TAG, "Detected BPM: %.1f", detected_bpm);
        }
    }
    
    // Save updated metadata ONLY if analysis completed fully
    // Partial saves would corrupt the waveform and BPM data
    if (analysis_complete) {
        ESP_LOGI(TAG, "Pass 2 complete: %d frames, BPM: %.1f", frames_decoded, meta.bpm);
        metadata_save(filepath, &meta);
    } else {
        ESP_LOGW(TAG, "Pass 2 interrupted at %d%%, not saving partial data", 
                 wave_index * 100 / WAVEFORM_POINTS);
    }
    
    // Cleanup
    MP3FreeDecoder(decoder);
    free(read_buffer);
    heap_caps_free(decode_buffer);
    fclose(f);
}

// ============================================================================
// Pre-Analysis Task
// ============================================================================

/**
 * @brief Pre-analysis background task
 */
static void preanalysis_task(void *pvParameters) {
    char filepath[PATH_MAX_LEN];
    
    while (1) {
        // Only process when completely idle
        if (state == ANALYZER_STATE_IDLE && 
            audio_player_get_state() == AUDIO_PLAYER_STATE_STOPPED) {
            
            if (xQueueReceive(preanalysis_queue, filepath, pdMS_TO_TICKS(1000)) == pdTRUE) {
                // Double-check not already analyzed
                if (!metadata_exists(filepath)) {
                    state = ANALYZER_STATE_PRE_ANALYZING;
                    ESP_LOGI(TAG, "Pre-analyzing: %s", filepath);
                    
                    // Run both passes
                    analyze_pass1(filepath);
                    
                    if (!suspend_requested && can_run_pass2()) {
                        analyze_pass2(filepath);
                    }
                    
                    state = ANALYZER_STATE_IDLE;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}
