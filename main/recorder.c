/**
 * @file recorder.c
 * @brief Master output recorder implementation
 * 
 * Records the master output to WAV files on the SD card.
 * Uses a ring buffer and background task to handle SD card writes
 * without blocking the audio playback task.
 */

#include "recorder.h"
#include "sd_card.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <errno.h>

static const char *TAG = "recorder";

// ============================================================================
// WAV File Format Definitions
// ============================================================================

#pragma pack(push, 1)
typedef struct {
    // RIFF chunk
    char riff_id[4];           // "RIFF"
    uint32_t riff_size;        // File size - 8
    char wave_id[4];           // "WAVE"
    
    // Format chunk
    char fmt_id[4];            // "fmt "
    uint32_t fmt_size;         // 16 for PCM
    uint16_t audio_format;     // 1 for PCM
    uint16_t num_channels;     // 2 for stereo
    uint32_t sample_rate;      // 44100
    uint32_t byte_rate;        // sample_rate * num_channels * bits/8
    uint16_t block_align;      // num_channels * bits/8
    uint16_t bits_per_sample;  // 16
    
    // Data chunk
    char data_id[4];           // "data"
    uint32_t data_size;        // Number of bytes of audio data
} wav_header_t;
#pragma pack(pop)

// ============================================================================
// Configuration
// ============================================================================

#define RECORDER_RING_BUFFER_SIZE   (RECORDER_BUFFER_FRAMES * RECORDER_CHANNELS * sizeof(int16_t) * 4)
#define RECORDER_WRITE_CHUNK_SIZE   (4096)  // Write to SD in 4KB chunks
#define RECORDER_TASK_STACK_SIZE    (4096)
#define RECORDER_TASK_PRIORITY      (3)     // Lower priority than playback

// Level meter constants
#define LEVEL_METER_DECAY_RATE      (0.95f)   // Per-frame decay
#define LEVEL_METER_ATTACK_RATE     (0.3f)    // Per-frame attack
#define RMS_WINDOW_SIZE             (1024)    // Samples for RMS calculation
#define CLIPPING_THRESHOLD          (32000)   // Near max int16

// ============================================================================
// State
// ============================================================================

static struct {
    bool initialized;
    recorder_state_t state;
    
    // Recording file
    FILE *wav_file;
    char current_filename[RECORDER_MAX_FILENAME];
    char directory[RECORDER_MAX_FILENAME];
    uint32_t data_bytes_written;
    
    // Ring buffer for samples
    int16_t *ring_buffer;
    size_t ring_size;           // Size in samples (not bytes)
    volatile size_t write_head;
    volatile size_t read_head;
    volatile size_t available;
    SemaphoreHandle_t buffer_mutex;
    
    // Background writer task
    TaskHandle_t writer_task;
    volatile bool writer_running;
    
    // Auto-split
    bool auto_split_enabled;
    uint32_t files_created;
    
    // Level meter
    recorder_levels_t levels;
    float left_peak_hold;
    float right_peak_hold;
    uint32_t peak_hold_ms;
    uint32_t peak_hold_counter;
    uint32_t rms_sum_left;
    uint32_t rms_sum_right;
    uint32_t rms_count;
    
    // Statistics
    uint32_t total_frames_written;
    uint32_t buffer_overruns;
    
    // Callback
    recorder_event_cb_t event_callback;
    
} s_recorder = {
    .initialized = false,
    .state = RECORDER_STATE_IDLE,
    .wav_file = NULL,
    .directory = RECORDER_DEFAULT_DIR,
    .auto_split_enabled = true,
    .peak_hold_ms = 2000,  // 2 second peak hold default
};

// ============================================================================
// Forward Declarations
// ============================================================================

static void recorder_writer_task(void *pvParameters);
static bool create_wav_file(const char *filename);
static void finalize_wav_file(void);
static void update_level_meter(const int16_t *samples, size_t num_frames);
static void generate_filename(char *buffer, size_t size, const char *base_name);
static bool ensure_directory_exists(const char *path);

// ============================================================================
// Lifecycle
// ============================================================================

bool recorder_init(void) {
    if (s_recorder.initialized) {
        ESP_LOGW(TAG, "Recorder already initialized");
        return true;
    }
    
    // Check SD card is mounted
    if (!sd_card_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted - recorder disabled");
        return false;
    }
    
    ESP_LOGI(TAG, "Initializing recorder...");
    
    // Create directory if it doesn't exist
    if (!ensure_directory_exists(s_recorder.directory)) {
        ESP_LOGE(TAG, "Failed to create recordings directory: %s", s_recorder.directory);
        return false;
    }
    
    // Allocate ring buffer in PSRAM for better performance
    s_recorder.ring_size = RECORDER_RING_BUFFER_SIZE / sizeof(int16_t);
    s_recorder.ring_buffer = (int16_t*)heap_caps_malloc(
        RECORDER_RING_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!s_recorder.ring_buffer) {
        // Fall back to internal RAM
        s_recorder.ring_buffer = (int16_t*)malloc(RECORDER_RING_BUFFER_SIZE);
        if (!s_recorder.ring_buffer) {
            ESP_LOGE(TAG, "Failed to allocate ring buffer");
            return false;
        }
        ESP_LOGW(TAG, "Ring buffer allocated in internal RAM");
    } else {
        ESP_LOGI(TAG, "Ring buffer allocated in PSRAM (%d KB)", 
                 RECORDER_RING_BUFFER_SIZE / 1024);
    }
    
    memset(s_recorder.ring_buffer, 0, RECORDER_RING_BUFFER_SIZE);
    s_recorder.write_head = 0;
    s_recorder.read_head = 0;
    s_recorder.available = 0;
    
    // Create mutex
    s_recorder.buffer_mutex = xSemaphoreCreateMutex();
    if (!s_recorder.buffer_mutex) {
        ESP_LOGE(TAG, "Failed to create buffer mutex");
        free(s_recorder.ring_buffer);
        s_recorder.ring_buffer = NULL;
        return false;
    }
    
    // Create writer task
    s_recorder.writer_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        recorder_writer_task,
        "rec_writer",
        RECORDER_TASK_STACK_SIZE,
        NULL,
        RECORDER_TASK_PRIORITY,
        &s_recorder.writer_task,
        0  // Run on core 0 (different from audio on core 1)
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create writer task");
        vSemaphoreDelete(s_recorder.buffer_mutex);
        free(s_recorder.ring_buffer);
        s_recorder.ring_buffer = NULL;
        return false;
    }
    
    // Reset level meters
    memset(&s_recorder.levels, 0, sizeof(recorder_levels_t));
    s_recorder.left_peak_hold = 0;
    s_recorder.right_peak_hold = 0;
    
    s_recorder.initialized = true;
    s_recorder.state = RECORDER_STATE_IDLE;
    
    ESP_LOGI(TAG, "Recorder initialized - directory: %s", s_recorder.directory);
    return true;
}

void recorder_deinit(void) {
    if (!s_recorder.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing recorder...");
    
    // Stop recording if active
    recorder_stop();
    
    // Stop writer task
    s_recorder.writer_running = false;
    if (s_recorder.writer_task) {
        // Give task time to exit
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelete(s_recorder.writer_task);
        s_recorder.writer_task = NULL;
    }
    
    // Free resources
    if (s_recorder.buffer_mutex) {
        vSemaphoreDelete(s_recorder.buffer_mutex);
        s_recorder.buffer_mutex = NULL;
    }
    
    if (s_recorder.ring_buffer) {
        heap_caps_free(s_recorder.ring_buffer);
        s_recorder.ring_buffer = NULL;
    }
    
    s_recorder.initialized = false;
    ESP_LOGI(TAG, "Recorder deinitialized");
}

bool recorder_is_initialized(void) {
    return s_recorder.initialized;
}

// ============================================================================
// Recording Control
// ============================================================================

bool recorder_start(void) {
    return recorder_start_with_name(NULL);
}

bool recorder_start_with_name(const char *filename) {
    if (!s_recorder.initialized) {
        ESP_LOGE(TAG, "Recorder not initialized");
        return false;
    }
    
    if (s_recorder.state == RECORDER_STATE_RECORDING) {
        ESP_LOGW(TAG, "Already recording");
        return true;
    }
    
    // Generate filename if not provided
    char full_path[RECORDER_MAX_FILENAME];
    generate_filename(full_path, sizeof(full_path), filename);
    
    // Create WAV file
    if (!create_wav_file(full_path)) {
        s_recorder.state = RECORDER_STATE_ERROR;
        return false;
    }
    
    // Reset state
    xSemaphoreTake(s_recorder.buffer_mutex, portMAX_DELAY);
    s_recorder.write_head = 0;
    s_recorder.read_head = 0;
    s_recorder.available = 0;
    s_recorder.total_frames_written = 0;
    s_recorder.buffer_overruns = 0;
    xSemaphoreGive(s_recorder.buffer_mutex);
    
    // Reset level meters
    recorder_reset_levels();
    
    s_recorder.state = RECORDER_STATE_RECORDING;
    s_recorder.files_created++;
    
    ESP_LOGI(TAG, "Recording started: %s", s_recorder.current_filename);
    
    // Invoke callback
    if (s_recorder.event_callback) {
        s_recorder.event_callback(RECORDER_STATE_RECORDING, s_recorder.current_filename);
    }
    
    return true;
}

void recorder_stop(void) {
    if (s_recorder.state == RECORDER_STATE_IDLE) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping recording...");
    
    // Set state first to stop accepting new samples
    recorder_state_t prev_state = s_recorder.state;
    s_recorder.state = RECORDER_STATE_IDLE;
    
    // Wait for buffer to drain (with timeout)
    int drain_attempts = 0;
    while (s_recorder.available > 0 && drain_attempts < 50) {
        vTaskDelay(pdMS_TO_TICKS(20));
        drain_attempts++;
    }
    
    if (s_recorder.available > 0) {
        ESP_LOGW(TAG, "Buffer not fully drained: %zu samples remaining", s_recorder.available);
    }
    
    // Finalize file
    finalize_wav_file();
    
    ESP_LOGI(TAG, "Recording stopped: %u frames (%.1f seconds)", 
             s_recorder.total_frames_written,
             (float)s_recorder.total_frames_written / RECORDER_SAMPLE_RATE);
    
    // Invoke callback
    if (s_recorder.event_callback && prev_state != RECORDER_STATE_IDLE) {
        s_recorder.event_callback(RECORDER_STATE_IDLE, s_recorder.current_filename);
    }
}

void recorder_pause(void) {
    if (s_recorder.state == RECORDER_STATE_RECORDING) {
        s_recorder.state = RECORDER_STATE_PAUSED;
        ESP_LOGI(TAG, "Recording paused");
        
        if (s_recorder.event_callback) {
            s_recorder.event_callback(RECORDER_STATE_PAUSED, s_recorder.current_filename);
        }
    }
}

void recorder_resume(void) {
    if (s_recorder.state == RECORDER_STATE_PAUSED) {
        s_recorder.state = RECORDER_STATE_RECORDING;
        ESP_LOGI(TAG, "Recording resumed");
        
        if (s_recorder.event_callback) {
            s_recorder.event_callback(RECORDER_STATE_RECORDING, s_recorder.current_filename);
        }
    }
}

recorder_state_t recorder_get_state(void) {
    return s_recorder.state;
}

bool recorder_is_recording(void) {
    return s_recorder.state == RECORDER_STATE_RECORDING || 
           s_recorder.state == RECORDER_STATE_PAUSED;
}

// ============================================================================
// Track Split
// ============================================================================

void recorder_set_auto_split(bool enable) {
    s_recorder.auto_split_enabled = enable;
    ESP_LOGI(TAG, "Auto-split %s", enable ? "enabled" : "disabled");
}

bool recorder_get_auto_split(void) {
    return s_recorder.auto_split_enabled;
}

void recorder_split(const char *track_name) {
    if (!s_recorder.auto_split_enabled) {
        return;
    }
    
    if (s_recorder.state != RECORDER_STATE_RECORDING) {
        return;
    }
    
    ESP_LOGI(TAG, "Splitting recording for track: %s", track_name ? track_name : "(unnamed)");
    
    // Finalize current file
    finalize_wav_file();
    
    // Generate new filename
    char full_path[RECORDER_MAX_FILENAME];
    generate_filename(full_path, sizeof(full_path), track_name);
    
    // Create new file
    if (!create_wav_file(full_path)) {
        s_recorder.state = RECORDER_STATE_ERROR;
        ESP_LOGE(TAG, "Failed to create split file");
        return;
    }
    
    s_recorder.files_created++;
    
    ESP_LOGI(TAG, "Split to new file: %s", s_recorder.current_filename);
    
    if (s_recorder.event_callback) {
        s_recorder.event_callback(RECORDER_STATE_RECORDING, s_recorder.current_filename);
    }
}

// ============================================================================
// Audio Feed
// ============================================================================

void recorder_feed_samples(const int16_t *samples, size_t num_frames) {
    if (!s_recorder.initialized || s_recorder.state != RECORDER_STATE_RECORDING) {
        return;
    }
    
    if (!samples || num_frames == 0) {
        return;
    }
    
    // Update level meter (always, even if buffer is full)
    update_level_meter(samples, num_frames);
    
    size_t num_samples = num_frames * RECORDER_CHANNELS;
    
    xSemaphoreTake(s_recorder.buffer_mutex, portMAX_DELAY);
    
    // Check for buffer space
    size_t free_space = s_recorder.ring_size - s_recorder.available;
    
    if (num_samples > free_space) {
        // Buffer overrun - drop samples
        s_recorder.buffer_overruns++;
        xSemaphoreGive(s_recorder.buffer_mutex);
        return;
    }
    
    // Copy samples to ring buffer
    size_t write_pos = s_recorder.write_head;
    size_t space_to_end = s_recorder.ring_size - write_pos;
    
    if (num_samples <= space_to_end) {
        // Single contiguous copy
        memcpy(&s_recorder.ring_buffer[write_pos], samples, num_samples * sizeof(int16_t));
    } else {
        // Wrap around
        memcpy(&s_recorder.ring_buffer[write_pos], samples, space_to_end * sizeof(int16_t));
        memcpy(&s_recorder.ring_buffer[0], &samples[space_to_end], 
               (num_samples - space_to_end) * sizeof(int16_t));
    }
    
    s_recorder.write_head = (write_pos + num_samples) % s_recorder.ring_size;
    s_recorder.available += num_samples;
    
    xSemaphoreGive(s_recorder.buffer_mutex);
}

// ============================================================================
// Level Meter
// ============================================================================

void recorder_get_levels(recorder_levels_t *levels) {
    if (!levels) return;
    
    // Copy current levels
    *levels = s_recorder.levels;
}

void recorder_reset_levels(void) {
    memset(&s_recorder.levels, 0, sizeof(recorder_levels_t));
    s_recorder.left_peak_hold = 0;
    s_recorder.right_peak_hold = 0;
    s_recorder.peak_hold_counter = 0;
    s_recorder.rms_sum_left = 0;
    s_recorder.rms_sum_right = 0;
    s_recorder.rms_count = 0;
}

void recorder_set_peak_hold(uint32_t hold_ms) {
    s_recorder.peak_hold_ms = hold_ms;
}

static void update_level_meter(const int16_t *samples, size_t num_frames) {
    if (num_frames == 0) return;
    
    int32_t max_left = 0;
    int32_t max_right = 0;
    uint64_t sum_sq_left = 0;
    uint64_t sum_sq_right = 0;
    bool clip_left = false;
    bool clip_right = false;
    
    for (size_t i = 0; i < num_frames; i++) {
        int16_t left = samples[i * 2];
        int16_t right = samples[i * 2 + 1];
        
        // Peak detection
        int32_t abs_left = left < 0 ? -left : left;
        int32_t abs_right = right < 0 ? -right : right;
        
        if (abs_left > max_left) max_left = abs_left;
        if (abs_right > max_right) max_right = abs_right;
        
        // Clipping detection
        if (abs_left >= CLIPPING_THRESHOLD) clip_left = true;
        if (abs_right >= CLIPPING_THRESHOLD) clip_right = true;
        
        // RMS accumulation
        sum_sq_left += (int32_t)left * left;
        sum_sq_right += (int32_t)right * right;
    }
    
    // Convert to normalized values (0.0 to 1.0)
    float peak_left = (float)max_left / 32768.0f;
    float peak_right = (float)max_right / 32768.0f;
    
    // Apply attack/decay
    if (peak_left > s_recorder.levels.left_peak) {
        s_recorder.levels.left_peak = peak_left * LEVEL_METER_ATTACK_RATE + 
                                       s_recorder.levels.left_peak * (1.0f - LEVEL_METER_ATTACK_RATE);
    } else {
        s_recorder.levels.left_peak *= LEVEL_METER_DECAY_RATE;
    }
    
    if (peak_right > s_recorder.levels.right_peak) {
        s_recorder.levels.right_peak = peak_right * LEVEL_METER_ATTACK_RATE + 
                                        s_recorder.levels.right_peak * (1.0f - LEVEL_METER_ATTACK_RATE);
    } else {
        s_recorder.levels.right_peak *= LEVEL_METER_DECAY_RATE;
    }
    
    // Peak hold
    if (peak_left > s_recorder.left_peak_hold) {
        s_recorder.left_peak_hold = peak_left;
        s_recorder.peak_hold_counter = 0;
    }
    if (peak_right > s_recorder.right_peak_hold) {
        s_recorder.right_peak_hold = peak_right;
        s_recorder.peak_hold_counter = 0;
    }
    
    // Update peak hold counter
    uint32_t frames_per_ms = RECORDER_SAMPLE_RATE / 1000;
    s_recorder.peak_hold_counter += num_frames;
    if (s_recorder.peak_hold_counter > s_recorder.peak_hold_ms * frames_per_ms) {
        // Release peak hold
        s_recorder.left_peak_hold *= 0.9f;
        s_recorder.right_peak_hold *= 0.9f;
    }
    
    // RMS calculation
    s_recorder.rms_sum_left += sum_sq_left / num_frames;
    s_recorder.rms_sum_right += sum_sq_right / num_frames;
    s_recorder.rms_count++;
    
    if (s_recorder.rms_count >= RMS_WINDOW_SIZE / num_frames) {
        float rms_left = sqrtf((float)s_recorder.rms_sum_left / s_recorder.rms_count) / 32768.0f;
        float rms_right = sqrtf((float)s_recorder.rms_sum_right / s_recorder.rms_count) / 32768.0f;
        
        s_recorder.levels.left_rms = rms_left;
        s_recorder.levels.right_rms = rms_right;
        
        s_recorder.rms_sum_left = 0;
        s_recorder.rms_sum_right = 0;
        s_recorder.rms_count = 0;
    }
    
    // Clipping (sticky until reset)
    if (clip_left) s_recorder.levels.clipping_left = true;
    if (clip_right) s_recorder.levels.clipping_right = true;
}

// ============================================================================
// Statistics
// ============================================================================

void recorder_get_stats(recorder_stats_t *stats) {
    if (!stats) return;
    
    stats->duration_ms = (uint32_t)((uint64_t)s_recorder.total_frames_written * 1000 / RECORDER_SAMPLE_RATE);
    stats->bytes_written = s_recorder.data_bytes_written;
    stats->files_created = s_recorder.files_created;
    stats->buffer_overruns = s_recorder.buffer_overruns;
    stats->disk_usage_mb = (float)s_recorder.data_bytes_written / (1024.0f * 1024.0f);
}

const char* recorder_get_current_file(void) {
    if (s_recorder.state == RECORDER_STATE_IDLE) {
        return NULL;
    }
    return s_recorder.current_filename;
}

uint32_t recorder_get_remaining_time(void) {
    // TODO: Implement using statvfs to get free space
    // For now, return a large value
    return 3600 * 24;  // 24 hours
}

// ============================================================================
// Event Callbacks
// ============================================================================

void recorder_set_event_callback(recorder_event_cb_t callback) {
    s_recorder.event_callback = callback;
}

// ============================================================================
// Configuration
// ============================================================================

bool recorder_set_directory(const char *path) {
    if (!path || strlen(path) >= RECORDER_MAX_FILENAME - 1) {
        return false;
    }
    
    strncpy(s_recorder.directory, path, RECORDER_MAX_FILENAME - 1);
    s_recorder.directory[RECORDER_MAX_FILENAME - 1] = '\0';
    
    return ensure_directory_exists(s_recorder.directory);
}

const char* recorder_get_directory(void) {
    return s_recorder.directory;
}

// ============================================================================
// Internal: WAV File Handling
// ============================================================================

static bool create_wav_file(const char *full_path) {
    // Copy filename
    strncpy(s_recorder.current_filename, full_path, RECORDER_MAX_FILENAME - 1);
    s_recorder.current_filename[RECORDER_MAX_FILENAME - 1] = '\0';
    
    // Open file for writing
    s_recorder.wav_file = fopen(full_path, "wb");
    if (!s_recorder.wav_file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s (errno=%d)", full_path, errno);
        return false;
    }
    
    // Write WAV header with placeholder size
    wav_header_t header = {
        .riff_id = {'R', 'I', 'F', 'F'},
        .riff_size = 0,  // Will be updated when file is closed
        .wave_id = {'W', 'A', 'V', 'E'},
        .fmt_id = {'f', 'm', 't', ' '},
        .fmt_size = 16,
        .audio_format = 1,  // PCM
        .num_channels = RECORDER_CHANNELS,
        .sample_rate = RECORDER_SAMPLE_RATE,
        .byte_rate = RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * (RECORDER_BITS_PER_SAMPLE / 8),
        .block_align = RECORDER_CHANNELS * (RECORDER_BITS_PER_SAMPLE / 8),
        .bits_per_sample = RECORDER_BITS_PER_SAMPLE,
        .data_id = {'d', 'a', 't', 'a'},
        .data_size = 0   // Will be updated when file is closed
    };
    
    if (fwrite(&header, sizeof(wav_header_t), 1, s_recorder.wav_file) != 1) {
        ESP_LOGE(TAG, "Failed to write WAV header");
        fclose(s_recorder.wav_file);
        s_recorder.wav_file = NULL;
        return false;
    }
    
    s_recorder.data_bytes_written = 0;
    
    ESP_LOGI(TAG, "Created WAV file: %s", full_path);
    return true;
}

static void finalize_wav_file(void) {
    if (!s_recorder.wav_file) {
        return;
    }
    
    // Update WAV header with final sizes
    uint32_t file_size = sizeof(wav_header_t) - 8 + s_recorder.data_bytes_written;
    
    // Seek to riff_size and write
    fseek(s_recorder.wav_file, 4, SEEK_SET);
    fwrite(&file_size, sizeof(uint32_t), 1, s_recorder.wav_file);
    
    // Seek to data_size and write
    fseek(s_recorder.wav_file, sizeof(wav_header_t) - 4, SEEK_SET);
    fwrite(&s_recorder.data_bytes_written, sizeof(uint32_t), 1, s_recorder.wav_file);
    
    // Close file
    fclose(s_recorder.wav_file);
    s_recorder.wav_file = NULL;
    
    ESP_LOGI(TAG, "Finalized WAV file: %u bytes", s_recorder.data_bytes_written);
}

// ============================================================================
// Internal: Background Writer Task
// ============================================================================

static void recorder_writer_task(void *pvParameters) {
    static int16_t write_buffer[RECORDER_WRITE_CHUNK_SIZE / sizeof(int16_t)];
    const size_t chunk_samples = sizeof(write_buffer) / sizeof(int16_t);
    
    ESP_LOGI(TAG, "Writer task started on core %d", xPortGetCoreID());
    
    while (s_recorder.writer_running) {
        // Check if we have enough data and a file is open
        if (s_recorder.wav_file && s_recorder.available >= chunk_samples) {
            xSemaphoreTake(s_recorder.buffer_mutex, portMAX_DELAY);
            
            // Read from ring buffer
            size_t read_pos = s_recorder.read_head;
            size_t space_to_end = s_recorder.ring_size - read_pos;
            
            if (chunk_samples <= space_to_end) {
                memcpy(write_buffer, &s_recorder.ring_buffer[read_pos], 
                       chunk_samples * sizeof(int16_t));
            } else {
                memcpy(write_buffer, &s_recorder.ring_buffer[read_pos], 
                       space_to_end * sizeof(int16_t));
                memcpy(&write_buffer[space_to_end], &s_recorder.ring_buffer[0], 
                       (chunk_samples - space_to_end) * sizeof(int16_t));
            }
            
            s_recorder.read_head = (read_pos + chunk_samples) % s_recorder.ring_size;
            s_recorder.available -= chunk_samples;
            
            xSemaphoreGive(s_recorder.buffer_mutex);
            
            // Write to file (outside mutex)
            size_t bytes_to_write = chunk_samples * sizeof(int16_t);
            size_t bytes_written = fwrite(write_buffer, 1, bytes_to_write, s_recorder.wav_file);
            
            if (bytes_written != bytes_to_write) {
                ESP_LOGE(TAG, "Write error: expected %zu, wrote %zu", bytes_to_write, bytes_written);
                s_recorder.state = RECORDER_STATE_ERROR;
            } else {
                s_recorder.data_bytes_written += bytes_written;
                s_recorder.total_frames_written += chunk_samples / RECORDER_CHANNELS;
            }
        } else {
            // Nothing to write - yield
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    ESP_LOGI(TAG, "Writer task exiting");
    vTaskDelete(NULL);
}

// ============================================================================
// Internal: Helpers
// ============================================================================

// Disable format-truncation warning - snprintf safely truncates
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static void generate_filename(char *buffer, size_t size, const char *base_name) {
    // Get current time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (base_name && strlen(base_name) > 0) {
        // Sanitize track name (replace unsafe characters)
        char safe_name[64];
        strncpy(safe_name, base_name, sizeof(safe_name) - 1);
        safe_name[sizeof(safe_name) - 1] = '\0';
        
        // Replace unsafe characters
        for (char *p = safe_name; *p; p++) {
            if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || 
                *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
                *p = '_';
            }
        }
        
        snprintf(buffer, size, "%s/%04d%02d%02d_%02d%02d%02d_%s.wav",
                 s_recorder.directory,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                 safe_name);
    } else {
        snprintf(buffer, size, "%s/%04d%02d%02d_%02d%02d%02d.wav",
                 s_recorder.directory,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}

#pragma GCC diagnostic pop

static bool ensure_directory_exists(const char *path) {
    struct stat st;
    
    if (stat(path, &st) == 0) {
        // Path exists
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        ESP_LOGE(TAG, "Path exists but is not a directory: %s", path);
        return false;
    }
    
    // Create directory
    if (mkdir(path, 0755) == 0) {
        ESP_LOGI(TAG, "Created directory: %s", path);
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to create directory: %s (errno=%d)", path, errno);
    return false;
}
