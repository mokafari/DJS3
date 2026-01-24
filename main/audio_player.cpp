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
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "AudioFileSourceFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

static const char *TAG = "audio_player";
static audio_player_state_t player_state = AUDIO_PLAYER_STATE_STOPPED;
static char current_filepath[256] = {0};
static uint32_t current_position = 0;
static uint32_t current_duration = 0;

static AudioFileSourceFS *file_source = nullptr;
static AudioGeneratorMP3 *mp3_gen = nullptr;
static AudioOutputI2S *audio_out = nullptr;

bool audio_player_init(void) {
    ESP_LOGI(TAG, "Initializing audio player");
    
    if (!audio_output_init()) {
        ESP_LOGE(TAG, "Failed to initialize audio output");
        return false;
    }
    
    player_state = AUDIO_PLAYER_STATE_STOPPED;
    current_filepath[0] = '\0';
    current_position = 0;
    current_duration = 0;
    
    ESP_LOGI(TAG, "Audio player initialized");
    return true;
}

void audio_player_deinit(void) {
    audio_player_stop();
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
    
    ESP_LOGI(TAG, "Loading track: %s", full_path);
    
    // Stop current playback
    audio_player_stop();
    
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
    
    strncpy(current_filepath, filepath, sizeof(current_filepath) - 1);
    current_filepath[sizeof(current_filepath) - 1] = '\0';
    current_position = 0;
    
    // Estimate duration from file size (rough estimate: ~1MB per minute at 128kbps)
    if (file_source) {
        uint32_t file_size = file_source->getSize();
        current_duration = (file_size / (128 * 1024 / 8)) * 60; // Rough estimate
    }
    
    ESP_LOGI(TAG, "Track loaded: %s", filepath);
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

#ifdef __cplusplus
}
#endif

