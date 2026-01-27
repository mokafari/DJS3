/**
 * @file audio_player.cpp
 * @brief Audio player implementation for MP3 playback
 * 
 * Note: This requires AudioGeneratorMP3 and libmad to be integrated
 * For now, this provides the interface structure
 */

#include "audio_player.h"
#include "audio_output.h"
#include "storage.h"
#include "granular_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "AudioFileSourceFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Forward declarations
static bool audio_player_load_simple(const char *full_path);
static bool audio_player_load_granular(const char *full_path);
static void audio_player_update_simple(void);
static void audio_player_update_granular(void);

static const char *TAG = "audio_player";
static audio_player_state_t player_state = AUDIO_PLAYER_STATE_STOPPED;
static audio_player_mode_t player_mode = AUDIO_PLAYER_MODE_SIMPLE;
static char current_filepath[256] = {0};
static uint32_t current_position = 0;
static uint32_t current_duration = 0;

// Simple mode
static AudioFileSourceFS *file_source = nullptr;
static AudioGeneratorMP3 *mp3_gen = nullptr;
static AudioOutputI2S *audio_out = nullptr;

// Granular mode
static granular_engine_t granular_engine;
static int16_t *granular_buffer = nullptr;
static bool decoder_paused = false;

// Buffer size: 8 bars @ 70 BPM = 27.4 seconds @ 44.1kHz stereo
// 27.4s * 44100 Hz * 2 channels * 2 bytes = 4,830,720 bytes ≈ 4.83 MB
#define GRANULAR_BUFFER_SIZE_BYTES (4830 * 1024)
#define GRANULAR_BUFFER_SAMPLES (GRANULAR_BUFFER_SIZE_BYTES / 4) // Stereo 16-bit, but we use mono samples

// Streaming thresholds (in samples)
#define LOOP_LIMIT (GRANULAR_BUFFER_SAMPLES)      // 8 bars ahead
#define REFILL_THRESHOLD (GRANULAR_BUFFER_SAMPLES / 2) // 4 bars ahead

bool audio_player_init(void) {
    ESP_LOGI(TAG, "Initializing audio player");
    
    if (!audio_output_init()) {
        ESP_LOGE(TAG, "Failed to initialize audio output");
        return false;
    }
    
    player_state = AUDIO_PLAYER_STATE_STOPPED;
    player_mode = AUDIO_PLAYER_MODE_SIMPLE;
    current_filepath[0] = '\0';
    current_position = 0;
    current_duration = 0;
    decoder_paused = false;
    
    ESP_LOGI(TAG, "Audio player initialized");
    return true;
}

void audio_player_deinit(void) {
    audio_player_stop();
    
    // Free granular buffer if allocated
    if (granular_buffer) {
        heap_caps_free(granular_buffer);
        granular_buffer = nullptr;
    }
    
    audio_output_deinit();
    ESP_LOGI(TAG, "Audio player deinitialized");
}

bool audio_player_load(const char *filepath) {
    if (!filepath) {
        ESP_LOGE(TAG, "Invalid filepath");
        return false;
    }
    
    if (!storage_is_available()) {
        ESP_LOGE(TAG, "No storage available");
        return false;
    }
    
    const char *mount_point = storage_get_mount_point();
    if (!mount_point) {
        ESP_LOGE(TAG, "Storage not mounted");
        return false;
    }
    
    // Build full path
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", mount_point, filepath);
    
    ESP_LOGI(TAG, "Loading track: %s (mode: %s)", full_path, 
             player_mode == AUDIO_PLAYER_MODE_GRANULAR ? "granular" : "simple");
    
    // Stop current playback
    audio_player_stop();
    
    if (player_mode == AUDIO_PLAYER_MODE_GRANULAR) {
        return audio_player_load_granular(full_path);
    } else {
        return audio_player_load_simple(full_path);
    }
}

/**
 * @brief Load track in simple mode
 */
static bool audio_player_load_simple(const char *full_path) {
    // Create file source
    file_source = new AudioFileSourceFS(full_path);
    if (!file_source || !file_source->isOpen()) {
        ESP_LOGE(TAG, "Failed to open file: %s", full_path);
        if (file_source) {
            delete file_source;
            file_source = nullptr;
        }
        return false;
    }
    
    // Get audio output
    audio_out = audio_output_get_i2s();
    if (!audio_out) {
        ESP_LOGE(TAG, "Audio output not available");
        delete file_source;
        file_source = nullptr;
        return false;
    }
    
    // Create MP3 generator
    mp3_gen = new AudioGeneratorMP3();
    if (!mp3_gen) {
        ESP_LOGE(TAG, "Failed to create MP3 generator");
        delete file_source;
        file_source = nullptr;
        return false;
    }
    
    // Begin playback
    if (!mp3_gen->begin(file_source, static_cast<AudioOutput*>(audio_out))) {
        ESP_LOGE(TAG, "Failed to begin MP3 playback");
        delete mp3_gen;
        mp3_gen = nullptr;
        delete file_source;
        file_source = nullptr;
        return false;
    }
    
    // Estimate duration from file size
    if (file_source) {
        uint32_t file_size = file_source->getSize();
        current_duration = (file_size / (128 * 1024 / 8)) * 60; // Rough estimate
    }
    
    // Store filepath
    strncpy(current_filepath, full_path, sizeof(current_filepath) - 1);
    current_filepath[sizeof(current_filepath) - 1] = '\0';
    current_position = 0;
    
    ESP_LOGI(TAG, "Track loaded in simple mode");
    return true;
}

/**
 * @brief Load track in granular mode
 */
static bool audio_player_load_granular(const char *full_path) {
    // Allocate granular buffer in PSRAM if not already allocated
    if (!granular_buffer) {
        granular_buffer = (int16_t*)heap_caps_malloc(GRANULAR_BUFFER_SIZE_BYTES, MALLOC_CAP_SPIRAM);
        if (!granular_buffer) {
            ESP_LOGE(TAG, "Failed to allocate granular buffer in PSRAM");
            return false;
        }
        memset(granular_buffer, 0, GRANULAR_BUFFER_SIZE_BYTES);
        ESP_LOGI(TAG, "Allocated granular buffer: %d bytes", GRANULAR_BUFFER_SIZE_BYTES);
    }
    
    // Initialize granular engine
    if (granular_engine_init(&granular_engine, granular_buffer, GRANULAR_BUFFER_SAMPLES, 44100) != 0) {
        ESP_LOGE(TAG, "Failed to initialize granular engine");
        return false;
    }
    
    // Set to streaming mode
    granular_engine_set_mode(&granular_engine, GRANULAR_MODE_STREAMING);
    
    // Create file source for MP3 decoder
    file_source = new AudioFileSourceFS(full_path);
    if (!file_source || !file_source->isOpen()) {
        ESP_LOGE(TAG, "Failed to open file: %s", full_path);
        if (file_source) {
            delete file_source;
            file_source = nullptr;
        }
        return false;
    }
    
    // Estimate duration
    if (file_source) {
        uint32_t file_size = file_source->getSize();
        current_duration = (file_size / (128 * 1024 / 8)) * 60; // Rough estimate
    }
    
    // Store filepath
    strncpy(current_filepath, full_path, sizeof(current_filepath) - 1);
    current_filepath[sizeof(current_filepath) - 1] = '\0';
    current_position = 0;
    
    // TODO: Create custom AudioOutput that writes to granular buffer
    // For now, this is a placeholder - actual implementation requires
    // intercepting decoded samples from AudioGeneratorMP3
    
    ESP_LOGI(TAG, "Track loaded in granular mode (decoder integration pending)");
    return true;
}

bool audio_player_play(void) {
    if (player_state == AUDIO_PLAYER_STATE_PLAYING) {
        return true;
    }
    
    if (current_filepath[0] == '\0' || !mp3_gen) {
        ESP_LOGW(TAG, "No track loaded");
        return false;
    }
    
    if (mp3_gen->isRunning()) {
        player_state = AUDIO_PLAYER_STATE_PLAYING;
        ESP_LOGI(TAG, "Playback started");
        return true;
    }
    
    return false;
}

void audio_player_pause(void) {
    if (player_state == AUDIO_PLAYER_STATE_PLAYING) {
        player_state = AUDIO_PLAYER_STATE_PAUSED;
        ESP_LOGI(TAG, "Playback paused");
    }
}

void audio_player_resume(void) {
    if (player_state == AUDIO_PLAYER_STATE_PAUSED) {
        player_state = AUDIO_PLAYER_STATE_PLAYING;
        ESP_LOGI(TAG, "Playback resumed");
    }
}

void audio_player_stop(void) {
    if (mp3_gen) {
        mp3_gen->stop();
        delete mp3_gen;
        mp3_gen = nullptr;
    }
    if (file_source) {
        file_source->close();
        delete file_source;
        file_source = nullptr;
    }
    
    player_state = AUDIO_PLAYER_STATE_STOPPED;
    current_position = 0;
    current_filepath[0] = '\0';
    ESP_LOGI(TAG, "Playback stopped");
}

audio_player_state_t audio_player_get_state(void) {
    return player_state;
}

uint32_t audio_player_get_position(void) {
    if (file_source) {
        // Estimate position from file position (rough)
        uint32_t file_pos = file_source->getPos();
        uint32_t file_size = file_source->getSize();
        if (file_size > 0) {
            current_position = (file_pos * current_duration) / file_size;
        }
    }
    return current_position;
}

uint32_t audio_player_get_duration(void) {
    return current_duration;
}

bool audio_player_seek(uint32_t position) {
    if (!file_source || position > current_duration) {
        return false;
    }
    
    // Estimate file position
    uint32_t file_size = file_source->getSize();
    uint32_t seek_pos = (position * file_size) / current_duration;
    
    if (file_source->seek(seek_pos, SEEK_SET)) {
        current_position = position;
        // Need to restart MP3 decoder after seek
        if (mp3_gen) {
            mp3_gen->desync();
        }
        ESP_LOGI(TAG, "Seek to position: %d seconds", position);
        return true;
    }
    return false;
}

void audio_player_update(void) {
    if (player_mode == AUDIO_PLAYER_MODE_GRANULAR) {
        audio_player_update_granular();
    } else {
        audio_player_update_simple();
    }
}

/**
 * @brief Update player in simple mode
 */
static void audio_player_update_simple(void) {
    if (mp3_gen && player_state == AUDIO_PLAYER_STATE_PLAYING) {
        if (!mp3_gen->loop()) {
            // Playback finished
            ESP_LOGI(TAG, "Playback finished");
            audio_player_stop();
        } else {
            // Update position estimate
            if (file_source) {
                uint32_t file_pos = file_source->getPos();
                uint32_t file_size = file_source->getSize();
                if (file_size > 0) {
                    current_position = (file_pos * current_duration) / file_size;
                }
            }
        }
    }
}

/**
 * @brief Update player in granular mode
 */
static void audio_player_update_granular(void) {
    if (player_state == AUDIO_PLAYER_STATE_PLAYING) {
        // Check buffer distance and pause/resume decoder
        bool should_pause = granular_engine_check_buffer_distance(&granular_engine, 
                                                                  LOOP_LIMIT, 
                                                                  REFILL_THRESHOLD);
        
        if (should_pause && !decoder_paused) {
            // Pause decoder
            decoder_paused = true;
            ESP_LOGI(TAG, "Decoder paused (buffer full)");
        } else if (!should_pause && decoder_paused) {
            // Resume decoder
            decoder_paused = false;
            ESP_LOGI(TAG, "Decoder resumed (buffer needs refill)");
        }
        
        // TODO: Process granular engine and output to I2S
        // This requires implementing the custom AudioOutput class
        // For now, this is a placeholder
    }
}

bool audio_player_set_mode(audio_player_mode_t mode) {
    if (player_state != AUDIO_PLAYER_STATE_STOPPED) {
        ESP_LOGW(TAG, "Cannot change mode while playing");
        return false;
    }
    
    player_mode = mode;
    ESP_LOGI(TAG, "Player mode set to: %s", 
             mode == AUDIO_PLAYER_MODE_GRANULAR ? "granular" : "simple");
    return true;
}

audio_player_mode_t audio_player_get_mode(void) {
    return player_mode;
}

void audio_player_set_granular_speed(float speed) {
    if (player_mode == AUDIO_PLAYER_MODE_GRANULAR) {
        float grain_size = granular_engine.streaming.grain_size_samples;
        granular_engine_set_streaming_params(&granular_engine, speed, grain_size, 
                                            granular_engine.streaming.jitter_amount);
    }
}

void audio_player_set_granular_grain_size(float grain_size_ms) {
    if (player_mode == AUDIO_PLAYER_MODE_GRANULAR) {
        float grain_size_samples = (grain_size_ms * 44100.0f) / 1000.0f;
        granular_engine_set_streaming_params(&granular_engine, 
                                            granular_engine.streaming.speed, 
                                            grain_size_samples,
                                            granular_engine.streaming.jitter_amount);
    }
}

void audio_player_set_granular_pitch(float pitch) {
    if (player_mode == AUDIO_PLAYER_MODE_GRANULAR) {
        granular_params_t params = granular_engine.params;
        params.pitch_factor = pitch;
        granular_engine_set_params(&granular_engine, &params);
    }
}

void audio_player_set_granular_jitter(float jitter) {
    if (player_mode == AUDIO_PLAYER_MODE_GRANULAR) {
        float grain_size = granular_engine.streaming.grain_size_samples;
        granular_engine_set_streaming_params(&granular_engine, 
                                            granular_engine.streaming.speed, 
                                            grain_size,
                                            jitter);
    }
}

#ifdef __cplusplus
}
#endif

