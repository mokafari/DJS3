/**
 * @file audio_player.cpp
 * @brief Audio player with DSP Pipeline (Block Processing)
 * 
 * Features:
 * - Fixed-point linear interpolation resampler for pitch control
 * - 3-band EQ with polynomial soft limiter
 * - Block-based processing (256 stereo frames) for cache efficiency
 */

#include "audio_player.h"
#include "audio_output.h"
#include "storage.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "mp3dec.h"
#include "id3_parser.h"
#include "esp_attr.h"   // For IRAM_ATTR
#include "esp_dsp.h"    // For SIMD DSP operations
#include "dsp_engine.h" // Fixed-point resampler
#include "filter.h"     // 3-band EQ with soft limiter
#include "pitch_control.h" // Atomic pitch access

static const char *TAG = "audio_player";

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------

// 4MB Ring Buffer = ~23 seconds of 44.1kHz 16-bit Stereo
// 4 * 1024 * 1024 bytes
#define RING_BUFFER_SIZE (4 * 1024 * 1024)

// Waveform Resolution
// One peak byte per N PCM bytes
// 480 pixels width. 
// If we want 4MB to map to, say, a zoom level, we need a flexible structure.
// For now, fixed ratio: 1 peak byte per 1024 PCM bytes (512 samples).
// 4MB / 1024 = 4KB waveform buffer. 
// 512 samples * 480 pixels = 245,760 samples = ~5.5 seconds at 44.1kHz.
#define WAVEFORM_RATIO 1024
#define WAVEFORM_BUFFER_SIZE (RING_BUFFER_SIZE / WAVEFORM_RATIO)

// Decoder output chunk (1 MP3 frame is usually 1152 samples * 2 * 2 = 4608 bytes)
#define DECODE_CHUNK_SIZE 4608

// ESP32-S3 cache works in 32-byte lines - alignment prevents double cache transactions
#define S3_CACHE_ALIGN 32

// ---------------------------------------------------------
// State
// ---------------------------------------------------------

static uint32_t current_position_seconds = 0;
static uint32_t total_duration_seconds = 0;
static float current_gain = 0.5f;
static uint32_t current_sample_rate = 44100;
static uint32_t current_bitrate = 0;           // Actual bitrate from MP3 frame header
static bool duration_calculated = false;        // True once we've calculated real duration
static size_t audio_data_size = 0;              // File size minus ID3 tags

static volatile uint64_t total_bytes_played = 0;

// Monotonic waveform index counter (never wraps during playback)
// Used for smooth scroll delta calculations
static volatile uint64_t waveform_monotonic_index = 0;

// Player State
static audio_player_state_t player_state = AUDIO_PLAYER_STATE_STOPPED;
static audio_player_mode_t player_mode = AUDIO_PLAYER_MODE_SIMPLE;
static char current_track_title[128] = {0};
static char current_filepath[256] = {0};

// MP3 Decoder State
static HMP3Decoder hMP3Decoder = 0;
static FILE *mp3_file = NULL;
static uint32_t file_size = 0;

// Read buffer for MP3 file reading
static uint8_t *file_read_buffer = NULL; // 4KB
static int file_bytes_left = 0;
static uint8_t *file_read_ptr = NULL;

// Ring Buffers (PSRAM)
static uint8_t *pcm_ring_buffer = NULL;
static uint8_t *waveform_ring_buffer = NULL;

// Ring Buffer Pointers (Indices in bytes)
static volatile size_t rb_write_head = 0;
static volatile size_t rb_read_head = 0;
static volatile size_t rb_available = 0; // Bytes available to read

// EOF flag - set by decoder when file is fully decoded
static volatile bool decoder_eof = false;

// Tasks & Sync
static TaskHandle_t decoder_task_handle = NULL;
static TaskHandle_t playback_task_handle = NULL;
static QueueHandle_t command_queue = NULL;
static SemaphoreHandle_t buffer_mutex = NULL;

// DSP State
static resampler_state_t resampler_state;
static dj_eq_t main_eq;
static size_t rb_read_head_index = 0;  // Track position in SAMPLES (not bytes)

typedef enum {
    CMD_LOAD,
    CMD_PLAY,
    CMD_PAUSE,
    CMD_STOP
} player_cmd_type_t;

typedef struct {
    player_cmd_type_t type;
    char filepath[256];
} player_cmd_t;

// ---------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------

static int fill_file_buffer(void) {
    if (!mp3_file) return 0;
    
    // Move remaining data to start
    if (file_bytes_left > 0 && file_read_ptr != file_read_buffer) {
        memmove(file_read_buffer, file_read_ptr, file_bytes_left);
    }
    
    int bytes_to_read = 4096 - file_bytes_left;
    if (bytes_to_read > 0) {
        size_t read = fread(file_read_buffer + file_bytes_left, 1, bytes_to_read, mp3_file);
        file_bytes_left += read;
        file_read_ptr = file_read_buffer;
        if (read == 0) return 0; // EOF
    }
    return file_bytes_left;
}

static void internal_reset_buffer(void) {
    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
    rb_write_head = 0;
    rb_read_head = 0;
    rb_read_head_index = 0;  // Reset sample-based index for resampler
    rb_available = 0;
    total_bytes_played = 0;
    waveform_monotonic_index = 0;  // Reset monotonic counter for new track
    decoder_eof = false;  // Reset EOF flag for new track
    // Pre-fill waveform with silence (128 = center/zero, or 0 for amplitude mode)
    // We are using amplitude mode (0-255), so 0 is silence.
    memset(waveform_ring_buffer, 0, WAVEFORM_BUFFER_SIZE);
    
    // Reset DSP state to prevent transients
    dsp_resampler_reset(&resampler_state);
    dj_eq_reset(&main_eq);
    xSemaphoreGive(buffer_mutex);
}

static void internal_stop(void) {
    if (hMP3Decoder) {
        MP3FreeDecoder(hMP3Decoder);
        hMP3Decoder = 0;
    }
    if (mp3_file) {
        fclose(mp3_file);
        mp3_file = NULL;
    }
    player_state = AUDIO_PLAYER_STATE_STOPPED;
    current_bitrate = 0;
    duration_calculated = false;
    audio_data_size = 0;
    internal_reset_buffer();
}

static bool internal_load(const char *filepath) {
    internal_stop();
    
    // Clear old track title FIRST to ensure it doesn't persist
    memset(current_track_title, 0, sizeof(current_track_title));
    
    // Parse ID3 first to get offsets and metadata
    id3_tag_t tag;
    uint32_t start_offset = 0;
    memset(&tag, 0, sizeof(id3_tag_t));
    
    if (id3_parse_file(filepath, &tag)) {
        if (tag.has_tag && tag.tag_size > 0) {
            start_offset = tag.tag_size;
            ESP_LOGI(TAG, "Skipping ID3v2 tag: %lu bytes", (unsigned long)start_offset);
        }
        
        if (tag.title[0] != '\0') {
            char *src = tag.title;
            while (*src == ' ' || *src == '\0') src++;
            strncpy(current_track_title, src, sizeof(current_track_title) - 1);
        }
    }
    
    // Fallback title if ID3 failed or empty
    if (current_track_title[0] == '\0') {
        const char *filename = strrchr(filepath, '/');
        filename = filename ? filename + 1 : filepath;
        strncpy(current_track_title, filename, sizeof(current_track_title) - 1);
        char *ext = strrchr(current_track_title, '.');
        if (ext) *ext = '\0';
    }
    
    mp3_file = fopen(filepath, "rb");
    if (!mp3_file) return false;
    
    fseek(mp3_file, 0, SEEK_END);
    file_size = ftell(mp3_file);
    
    // Seek to start of audio data
    fseek(mp3_file, start_offset, SEEK_SET);
    
    hMP3Decoder = MP3InitDecoder();
    if (!hMP3Decoder) {
        fclose(mp3_file);
        mp3_file = NULL;
        return false;
    }
    
    file_bytes_left = 0;
    file_read_ptr = file_read_buffer;
    fill_file_buffer();
    
    strncpy(current_filepath, filepath, sizeof(current_filepath) - 1);
    
    // Store audio data size (file size minus ID3 tags) for duration calculation
    audio_data_size = file_size - start_offset;
    
    // Initial estimate at 192kbps (common bitrate) - will be refined after first frame decode
    // Formula: duration = bytes / (bitrate_kbps * 1000 / 8) = bytes * 8 / (bitrate_kbps * 1000)
    total_duration_seconds = (audio_data_size * 8) / (192 * 1000);
    current_position_seconds = 0;
    current_bitrate = 0;
    duration_calculated = false;
    
    // Start buffering immediately
    player_state = AUDIO_PLAYER_STATE_PAUSED; // Loaded but paused
    
    return true;
}

// ---------------------------------------------------------
// Decoder Task (Producer)
// ---------------------------------------------------------
static void decoder_task(void *pvParameters) {
    // Max MP3 frame size - must be 16-byte aligned for memcpy optimizations
    static __attribute__((aligned(16))) int16_t decode_buf[2304];
    static uint32_t decode_count = 0;
    static uint32_t last_debug_time = 0;
    
    ESP_LOGI(TAG, "Decoder task started on core %d", xPortGetCoreID());
    
    while (1) {
        // Handle commands
        player_cmd_t cmd;
        if (xQueueReceive(command_queue, &cmd, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Decoder received command: %d", cmd.type);
            switch (cmd.type) {
                case CMD_LOAD: 
                    ESP_LOGI(TAG, "CMD_LOAD: %s", cmd.filepath);
                    if (internal_load(cmd.filepath)) {
                        ESP_LOGI(TAG, "Track loaded successfully");
                    } else {
                        ESP_LOGE(TAG, "Failed to load track!");
                    }
                    break;
                case CMD_PLAY: 
                    ESP_LOGI(TAG, "CMD_PLAY: Starting playback");
                    player_state = AUDIO_PLAYER_STATE_PLAYING; 
                    break;
                case CMD_PAUSE: 
                    ESP_LOGI(TAG, "CMD_PAUSE: Pausing playback");
                    player_state = AUDIO_PLAYER_STATE_PAUSED; 
                    break;
                case CMD_STOP: 
                    ESP_LOGI(TAG, "CMD_STOP: Stopping playback");
                    internal_stop(); 
                    break;
            }
        }
        
        // Decode Loop
        // We decode IF we have a file AND the buffer isn't full
        bool buffer_full = false;
        
        xSemaphoreTake(buffer_mutex, portMAX_DELAY);
        size_t current_available = rb_available;
        if (current_available > RING_BUFFER_SIZE - DECODE_CHUNK_SIZE * 2) {
            buffer_full = true;
        }
        xSemaphoreGive(buffer_mutex);
        
        // Periodic debug output (every 5 seconds)
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_debug_time >= 5000) {
            ESP_LOGI(TAG, "Decoder: frames=%lu | buffer=%.1f%% | file=%s | eof=%d | bytes_left=%d",
                     decode_count, 
                     100.0f * current_available / RING_BUFFER_SIZE,
                     mp3_file ? "open" : "closed",
                     decoder_eof,
                     file_bytes_left);
            last_debug_time = now;
        }
        
        // Skip decoding if EOF already reached or file closed
        if (mp3_file && hMP3Decoder && !buffer_full && !decoder_eof) {
            int offset = MP3FindSyncWord(file_read_ptr, file_bytes_left);
            if (offset < 0) {
                if (fill_file_buffer() == 0) {
                    // EOF reached - signal to playback task
                    ESP_LOGI(TAG, "Decoder: EOF reached (sync search), all audio decoded");
                    decoder_eof = true;
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                offset = MP3FindSyncWord(file_read_ptr, file_bytes_left);
            }
            
            // Also check for EOF using feof() - backup detection
            if (mp3_file && feof(mp3_file) && file_bytes_left < 1024 && !decoder_eof) {
                ESP_LOGI(TAG, "Decoder: EOF reached (feof), all audio decoded");
                decoder_eof = true;
            }
            
            if (offset >= 0) {
                file_read_ptr += offset;
                file_bytes_left -= offset;
                
                int res = MP3Decode(hMP3Decoder, &file_read_ptr, &file_bytes_left, decode_buf, 0);
                
                if (res == ERR_MP3_NONE) {
                    MP3FrameInfo frameInfo;
                    MP3GetLastFrameInfo(hMP3Decoder, &frameInfo);
                    
                    // Update sample rate if it changed
                    if (frameInfo.samprate != (int)current_sample_rate && frameInfo.samprate > 0) {
                        current_sample_rate = frameInfo.samprate;
                        audio_output_set_rate(current_sample_rate);
                    }
                    
                    // Calculate accurate duration using actual bitrate from first frame
                    if (!duration_calculated && frameInfo.bitrate > 0) {
                        current_bitrate = frameInfo.bitrate;
                        // duration = audio_bytes * 8 / bitrate_bps
                        total_duration_seconds = (audio_data_size * 8) / current_bitrate;
                        duration_calculated = true;
                        ESP_LOGI("audio_player", "Duration calculated: %us (bitrate: %d kbps, audio size: %zu bytes)",
                                 total_duration_seconds, current_bitrate / 1000, audio_data_size);
                    }

                    size_t pcm_bytes = frameInfo.outputSamps * sizeof(int16_t);
                    
                    // 1. Write to PCM Ring Buffer
                    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
                    
                    // Handle wrap-around
                    size_t space_at_end = RING_BUFFER_SIZE - rb_write_head;
                    if (pcm_bytes <= space_at_end) {
                        memcpy(&pcm_ring_buffer[rb_write_head], decode_buf, pcm_bytes);
                    } else {
                        memcpy(&pcm_ring_buffer[rb_write_head], decode_buf, space_at_end);
                        memcpy(&pcm_ring_buffer[0], (uint8_t*)decode_buf + space_at_end, pcm_bytes - space_at_end);
                    }
                    
                    // 2. Update Waveform Ring Buffer (GAPLESS)
                    // We iterate through all samples to ensure no peaks are missed.
                    for (size_t i = 0; i < pcm_bytes; i += 2) {
                        size_t cur_pos = rb_write_head + i;
                        // If we cross into a new WAVEFORM_RATIO boundary, clear that index
                        if (cur_pos % WAVEFORM_RATIO == 0) {
                            waveform_ring_buffer[(cur_pos / WAVEFORM_RATIO) % WAVEFORM_BUFFER_SIZE] = 0;
                        }
                        
                        int16_t val = abs(((int16_t*)decode_buf)[i/2]);
                        uint8_t peak = (uint8_t)(val >> 7);
                        size_t w_idx = (cur_pos / WAVEFORM_RATIO) % WAVEFORM_BUFFER_SIZE;
                        
                        if (peak > waveform_ring_buffer[w_idx]) {
                            waveform_ring_buffer[w_idx] = peak;
                        }
                    }
                    
                    rb_write_head = (rb_write_head + pcm_bytes) % RING_BUFFER_SIZE;
                    rb_available += pcm_bytes;
                    if (rb_available > RING_BUFFER_SIZE) rb_available = RING_BUFFER_SIZE;
                    decode_count++;
                    
                    xSemaphoreGive(buffer_mutex);
                    
                } else if (res == ERR_MP3_INDATA_UNDERFLOW) {
                    if (fill_file_buffer() == 0) {
                        // EOF during underflow - no more data coming
                        if (!decoder_eof) {
                            ESP_LOGI(TAG, "Decoder: EOF reached (underflow), all audio decoded");
                            decoder_eof = true;
                        }
                    }
                } else {
                    // Skip invalid byte and try next
                    file_read_ptr++;
                    file_bytes_left--;
                    // If we've exhausted the buffer trying to find valid frames, check EOF
                    if (file_bytes_left == 0 && fill_file_buffer() == 0) {
                        if (!decoder_eof) {
                            ESP_LOGI(TAG, "Decoder: EOF reached (exhausted), all audio decoded");
                            decoder_eof = true;
                        }
                    }
                }
            }
            
            if (file_bytes_left < 1024) {
                if (fill_file_buffer() == 0 && !decoder_eof) {
                    ESP_LOGI(TAG, "Decoder: EOF reached (refill), all audio decoded");
                    decoder_eof = true;
                }
            }
            
            // Yield to allow other tasks (important!)
            vTaskDelay(1); 
            
        } else {
            // Idle state or buffer full
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// ---------------------------------------------------------
// Optimized Volume Function (IRAM for cache-miss immunity)
// Uses fixed-point Q15 math for efficient integer multiplication
// Note: dsps_mulc_s16 crashes on ESP32-S3, so we use manual loop
// ---------------------------------------------------------
static void IRAM_ATTR apply_volume_dsp(int16_t* buffer, int samples, float gain) {
    if (gain >= 1.0f) return; // No processing needed
    
    // Convert float gain (0.0 - 1.0) to Q15 fixed point (0 - 32767)
    int32_t gain_q15 = (int32_t)(gain * 32768.0f);
    
    // Manual loop with Q15 fixed-point multiplication
    // Compiler will optimize this for the ESP32-S3
    for (int i = 0; i < samples; i++) {
        int32_t sample = buffer[i];
        buffer[i] = (int16_t)((sample * gain_q15) >> 15);
    }
}

// ---------------------------------------------------------
// Playback Task (Consumer) - Block Processing DSP Pipeline
// ---------------------------------------------------------
static void playback_task(void *pvParameters) {
    // Fixed-size output block for consistent DSP processing
    // 256 stereo frames * 2 channels = 512 samples
    // MUST be 16-byte aligned for ESP-DSP SIMD operations
    static __attribute__((aligned(16))) int16_t i2s_block[DSP_BLOCK_SIZE * 2];
    static uint32_t blocks_played = 0;
    static uint32_t underruns = 0;
    static uint32_t last_debug_time = 0;
    
    // Ring buffer size in stereo frames (bytes / 4)
    const size_t ring_size_samples = RING_BUFFER_SIZE / 4;
    
    ESP_LOGI(TAG, "Playback task started on core %d (block size: %d frames)", 
             xPortGetCoreID(), DSP_BLOCK_SIZE);
    
    bool first_play = true;
    
    while (1) {
        // Check if we should play
        if (player_state != AUDIO_PLAYER_STATE_PLAYING) {
            first_play = true;  // Reset for next play
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Debug: Log on first playback frame
        if (first_play) {
            ESP_LOGI(TAG, "Playback starting - pcm_ring_buffer=%p rb_read_head_index=%zu", 
                     pcm_ring_buffer, rb_read_head_index);
            first_play = false;
        }
        
        // 1. Calculate playback speed from pitch control (atomic read)
        float pitch_percent = pitch_control_get();
        float speed_ratio = 1.0f + (pitch_percent / 100.0f);
        
        // Safety: prevent near-zero or negative speeds (would cause issues)
        if (speed_ratio < 0.1f) speed_ratio = 0.1f;
        if (speed_ratio > 2.0f) speed_ratio = 2.0f;
        
        // 2. Check buffer availability
        // Need ~1.5x source samples for safety margin at variable speeds
        size_t source_needed = (size_t)(DSP_BLOCK_SIZE * speed_ratio * 1.5f);
        
        xSemaphoreTake(buffer_mutex, portMAX_DELAY);
        size_t available_samples = rb_available / 4;  // Bytes to stereo frames
        
        if (available_samples >= source_needed) {
            // Safety check - verify buffer is valid
            if (!pcm_ring_buffer) {
                ESP_LOGE(TAG, "FATAL: pcm_ring_buffer is NULL!");
                xSemaphoreGive(buffer_mutex);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            
            // 3. Resample: Read from ring buffer at variable speed
            // This is the core pitch/tempo control engine
            size_t consumed = dsp_resample_linear(
                &resampler_state,
                (const int16_t*)pcm_ring_buffer,
                ring_size_samples,
                &rb_read_head_index,
                i2s_block,          // Output buffer
                DSP_BLOCK_SIZE,     // Requested output frames
                speed_ratio
            );
            
            // 4. Update buffer tracking
            size_t bytes_consumed = consumed * 4;  // Stereo frames to bytes
            rb_available -= bytes_consumed;
            rb_read_head = rb_read_head_index * 4;  // Sync byte-based head for waveform
            total_bytes_played += bytes_consumed;
            waveform_monotonic_index = total_bytes_played / WAVEFORM_RATIO;
            
            xSemaphoreGive(buffer_mutex);
            
            // 5. Apply 3-Band EQ with Soft Limiter
            dj_eq_process(&main_eq, i2s_block, DSP_BLOCK_SIZE);
            
            // 6. Apply Volume using SIMD-optimized DSP function
            // RE-ENABLED: Testing SIMD volume
            apply_volume_dsp(i2s_block, DSP_BLOCK_SIZE * 2, current_gain);
            
            // 7. Output to I2S (blocking write)
            audio_output_write(i2s_block, DSP_BLOCK_SIZE * 2);
            
            blocks_played++;
            
        } else {
            // Check if track has finished (EOF reached and buffer nearly empty)
            bool eof_flag = decoder_eof;  // Capture volatile value
            size_t avail = available_samples;  // Capture value
            
            xSemaphoreGive(buffer_mutex);
            
            // Debug: Log every 200 underruns to see what's happening
            static uint32_t underrun_debug_counter = 0;
            if (eof_flag && (underrun_debug_counter++ % 200 == 0)) {
                ESP_LOGW(TAG, "EOF check: eof=%d avail=%zu threshold=%d should_stop=%d",
                         eof_flag, avail, DSP_BLOCK_SIZE, (avail < DSP_BLOCK_SIZE));
            }
            
            if (eof_flag && avail <= DSP_BLOCK_SIZE) {
                ESP_LOGI(TAG, "Track finished - buffer drained after EOF (remaining: %zu samples)", avail);
                player_state = AUDIO_PLAYER_STATE_STOPPED;
                
                // Reset waveform to clear old data display
                xSemaphoreTake(buffer_mutex, portMAX_DELAY);
                memset(waveform_ring_buffer, 0, WAVEFORM_BUFFER_SIZE);
                xSemaphoreGive(buffer_mutex);
                
                // Reset playback state for next track
                first_play = true;
                underrun_debug_counter = 0;
                continue;
            }
            
            underruns++;
            // Buffer underrun - wait for decoder to catch up
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        
        // Periodic debug output
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_debug_time >= 5000) {
            ESP_LOGI(TAG, "Playback: blocks=%lu | underruns=%lu | played=%llu bytes | speed=%.2fx | eof=%d",
                     blocks_played, underruns, total_bytes_played, speed_ratio, decoder_eof);
            last_debug_time = now;
        }
    }
}

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------

bool audio_player_init(void) {
    ESP_LOGI(TAG, "Initializing Audio Player (DSP Pipeline)...");
    
    // Initialize DSP library for SIMD operations
    esp_err_t dsp_ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (dsp_ret == ESP_OK) {
        ESP_LOGI(TAG, "DSP library initialized for SIMD operations");
    } else {
        ESP_LOGW(TAG, "DSP init returned %d (non-critical, SIMD may still work)", dsp_ret);
    }
    
    // Initialize DSP components
    dsp_resampler_init(&resampler_state);
    dj_eq_init(&main_eq, 44100);
    
    // Allocate buffers in PSRAM with cache-line alignment for ESP32-S3
    ESP_LOGI(TAG, "Allocating audio buffers in PSRAM...");
    
    file_read_buffer = (uint8_t*)heap_caps_aligned_alloc(
        S3_CACHE_ALIGN, 4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "  file_read_buffer: %p (4KB)", file_read_buffer);
    
    pcm_ring_buffer = (uint8_t*)heap_caps_aligned_alloc(
        S3_CACHE_ALIGN, RING_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "  pcm_ring_buffer: %p (%d MB)", pcm_ring_buffer, RING_BUFFER_SIZE / (1024*1024));
    
    waveform_ring_buffer = (uint8_t*)heap_caps_aligned_alloc(
        S3_CACHE_ALIGN, WAVEFORM_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "  waveform_ring_buffer: %p (%d KB)", waveform_ring_buffer, WAVEFORM_BUFFER_SIZE / 1024);
    
    if (!file_read_buffer || !pcm_ring_buffer || !waveform_ring_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers!");
        ESP_LOGE(TAG, "  file_read: %p, pcm_ring: %p, waveform: %p", 
                 file_read_buffer, pcm_ring_buffer, waveform_ring_buffer);
        return false;
    }
    
    memset(waveform_ring_buffer, 0, WAVEFORM_BUFFER_SIZE);
    
    command_queue = xQueueCreate(10, sizeof(player_cmd_t));
    buffer_mutex = xSemaphoreCreateMutex();
    
    if (!audio_output_init()) return false;
    
    // Create Tasks
    // Increased playback stack for DSP processing
    xTaskCreatePinnedToCore(decoder_task, "decoder_task", 12288, NULL, 5, &decoder_task_handle, 1);
    xTaskCreatePinnedToCore(playback_task, "playback_task", 8192, NULL, 6, &playback_task_handle, 1);
    
    ESP_LOGI(TAG, "DSP Pipeline ready: Resampler + 3-Band EQ + Soft Limiter");
    return true;
}

void audio_player_get_waveform(uint8_t *buffer, size_t size) {
    if (!buffer || size == 0) return;
    
    // We want to return the waveform data CENTERED around the current playhead.
    // Use waveform_monotonic_index for stable positioning (doesn't wrap)
    
    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
    
    // Use monotonic index wrapped to buffer size for current position
    size_t current_wave_idx = waveform_monotonic_index % WAVEFORM_BUFFER_SIZE;
    
    // We want to fill 'size' pixels (e.g. 480).
    // Playhead should be at center (size/2).
    // So we need to read from (current_wave_idx - size/2).
    
    int start_idx = (int)current_wave_idx - (int)(size / 2);
    
    for (size_t i = 0; i < size; i++) {
        int idx = start_idx + i;
        
        // Handle wrapping for circular buffer lookup
        while (idx < 0) idx += WAVEFORM_BUFFER_SIZE;
        while (idx >= WAVEFORM_BUFFER_SIZE) idx -= WAVEFORM_BUFFER_SIZE;
        
        buffer[i] = waveform_ring_buffer[idx];
    }
    
    xSemaphoreGive(buffer_mutex);
}

size_t audio_player_get_waveform_index(void) {
    // Return monotonic index for stable scroll delta calculations
    // This never wraps during playback (unlike rb_read_head which wraps with ring buffer)
    return (size_t)waveform_monotonic_index;
}

// Wrappers for commands
bool audio_player_load(const char *filepath) {
    player_cmd_t cmd = { .type = CMD_LOAD, .filepath = {0} };
    strncpy(cmd.filepath, filepath, sizeof(cmd.filepath)-1);
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
    return true;
}

bool audio_player_play(void) {
    player_cmd_t cmd = { .type = CMD_PLAY, .filepath = {0} };
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
    return true;
}

void audio_player_stop(void) {
    player_cmd_t cmd = { .type = CMD_STOP, .filepath = {0} };
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
}

void audio_player_pause(void) {
    player_cmd_t cmd = { .type = CMD_PAUSE, .filepath = {0} };
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
}

void audio_player_resume(void) { audio_player_play(); }
void audio_player_update(void) { } // No-op
const char* audio_player_get_track_title(void) { return current_track_title; }
void audio_player_set_gain(float gain) { current_gain = gain; }
audio_player_state_t audio_player_get_state(void) { return player_state; }
uint32_t audio_player_get_duration(void) { return total_duration_seconds; }

uint32_t audio_player_get_position(void) {
    // Calculate precise position based on bytes played
    // 16-bit stereo = 4 bytes per sample frame.
    // 44100 Hz * 4 = 176400 bytes/sec
    if (total_bytes_played > 0 && current_sample_rate > 0) {
        uint32_t bytes_per_sec = current_sample_rate * 4;
        return (uint32_t)(total_bytes_played / bytes_per_sec);
    }
    return 0;
}

float audio_player_get_precise_position(void) {
    if (total_bytes_played > 0 && current_sample_rate > 0) {
        return (float)total_bytes_played / (float)(current_sample_rate * 4);
    }
    return 0.0f;
}

// Stubs
void audio_player_deinit(void) {}
bool audio_player_seek(uint32_t position) { return false; }
bool audio_player_set_mode(audio_player_mode_t mode) { return true; }
audio_player_mode_t audio_player_get_mode(void) { return player_mode; }
void audio_player_set_granular_speed(float speed) {}
void audio_player_set_granular_grain_size(float grain_size_ms) {}
void audio_player_set_granular_pitch(float pitch) {}
void audio_player_set_granular_jitter(float jitter) {}
