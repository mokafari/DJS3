/**
 * @file audio_player.cpp
 * @brief Audio player implementation using libhelix-mp3
 */

#include "audio_player.h"
#include "audio_output.h"
#include "storage.h"
#include "granular_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "mp3dec.h"

static const char *TAG = "audio_player";

// MP3 decoding buffer sizes
#define READ_BUFFER_SIZE (4 * 1024)
#define OUTPUT_BUFFER_SIZE (MAX_NSAMP * MAX_NGRAN * MAX_NCHAN * sizeof(short)) 

static audio_player_state_t player_state = AUDIO_PLAYER_STATE_STOPPED;
static audio_player_mode_t player_mode = AUDIO_PLAYER_MODE_SIMPLE;
static char current_filepath[512] = {0};
static uint32_t current_position = 0;
static uint32_t current_duration = 0;

// Decoder state
static HMP3Decoder hMP3Decoder = 0;
static FILE *mp3_file = NULL;
static uint8_t *read_buffer = NULL;
static int16_t *output_buffer = NULL;
static int bytes_left = 0;
static uint8_t *read_ptr = NULL;
static uint32_t file_size = 0;

// Granular mode
static granular_engine_t granular_engine;
static int16_t *granular_buffer = nullptr;

bool audio_player_init(void) {
    ESP_LOGI(TAG, "Initializing audio player (Helix)");
    
    if (!audio_output_init()) {
        ESP_LOGE(TAG, "Failed to initialize audio output");
        return false;
    }
    
    // Allocate buffers in PSRAM
    read_buffer = (uint8_t*)heap_caps_malloc(READ_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    output_buffer = (int16_t*)heap_caps_malloc(OUTPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    
    if (!read_buffer || !output_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 buffers");
        return false;
    }
    
    player_state = AUDIO_PLAYER_STATE_STOPPED;
    player_mode = AUDIO_PLAYER_MODE_SIMPLE;
    
    ESP_LOGI(TAG, "Audio player initialized");
    return true;
}

void audio_player_deinit(void) {
    audio_player_stop();
    
    if (read_buffer) heap_caps_free(read_buffer);
    if (output_buffer) heap_caps_free(output_buffer);
    if (granular_buffer) heap_caps_free(granular_buffer);
    
    audio_output_deinit();
}

static int fill_read_buffer(void) {
    if (!mp3_file) return 0;
    
    // Move remaining data to start
    if (bytes_left > 0 && read_ptr != read_buffer) {
        memmove(read_buffer, read_ptr, bytes_left);
    }
    
    int bytes_to_read = READ_BUFFER_SIZE - bytes_left;
    if (bytes_to_read > 0) {
        size_t read = fread(read_buffer + bytes_left, 1, bytes_to_read, mp3_file);
        bytes_left += read;
        read_ptr = read_buffer;
        
        if (read == 0) {
            return 0; // EOF
        }
    }
    return bytes_left;
}

bool audio_player_load(const char *filepath) {
    audio_player_stop();
    
    ESP_LOGI(TAG, "Loading file: %s", filepath);
    
    mp3_file = fopen(filepath, "rb");
    if (!mp3_file) {
        ESP_LOGE(TAG, "Failed to open file");
        return false;
    }
    
    fseek(mp3_file, 0, SEEK_END);
    file_size = ftell(mp3_file);
    fseek(mp3_file, 0, SEEK_SET);
    
    // Create decoder
    hMP3Decoder = MP3InitDecoder();
    if (!hMP3Decoder) {
        ESP_LOGE(TAG, "Failed to init Helix decoder");
        fclose(mp3_file);
        mp3_file = NULL;
        return false;
    }
    
    // Initial buffer fill
    bytes_left = 0;
    read_ptr = read_buffer;
    fill_read_buffer();
    
    strncpy(current_filepath, filepath, sizeof(current_filepath) - 1);
    player_state = AUDIO_PLAYER_STATE_STOPPED; // Ready but stopped
    current_position = 0;
    
    // Estimate duration
    current_duration = file_size / (128 * 1024 / 8); 
    
    return true;
}

bool audio_player_play(void) {
    if (mp3_file && hMP3Decoder) {
        player_state = AUDIO_PLAYER_STATE_PLAYING;
        ESP_LOGI(TAG, "Playback started");
        return true;
    }
    return false;
}

void audio_player_stop(void) {
    if (hMP3Decoder) {
        MP3FreeDecoder(hMP3Decoder);
        hMP3Decoder = 0;
    }
    if (mp3_file) {
        fclose(mp3_file);
        mp3_file = NULL;
    }
    player_state = AUDIO_PLAYER_STATE_STOPPED;
}

void audio_player_pause(void) {
    if (player_state == AUDIO_PLAYER_STATE_PLAYING) {
        player_state = AUDIO_PLAYER_STATE_PAUSED;
    }
}

void audio_player_resume(void) {
    if (player_state == AUDIO_PLAYER_STATE_PAUSED) {
        player_state = AUDIO_PLAYER_STATE_PLAYING;
    }
}

void audio_player_update(void) {
    if (player_state != AUDIO_PLAYER_STATE_PLAYING) return;
    
    // Find sync word
    int offset = MP3FindSyncWord(read_ptr, bytes_left);
    if (offset < 0) {
        // No sync word found, refill
        if (fill_read_buffer() == 0) {
            audio_player_stop(); // EOF
            ESP_LOGI(TAG, "Playback finished");
        }
        return;
    }
    
    read_ptr += offset;
    bytes_left -= offset;
    
    // Decode frame
    int res = MP3Decode(hMP3Decoder, &read_ptr, &bytes_left, output_buffer, 0);
    
    if (res == ERR_MP3_NONE) {
        // Get frame info
        MP3FrameInfo frameInfo;
        MP3GetLastFrameInfo(hMP3Decoder, &frameInfo);
        
        // Write to audio output (stereo samples)
        // Helix outputs interleaved stereo if 2 channels
        size_t samples = frameInfo.outputSamps;
        
        // Check samplerate match (simple handling)
        if (frameInfo.samprate != audio_output_get_rate()) {
            // TODO: Reconfigure I2S if needed, for now assume 44.1k
        }
        
        audio_output_write(output_buffer, samples);
        
        // Update position (approx)
        if (file_size > 0 && mp3_file) {
            long pos = ftell(mp3_file);
            current_position = (pos * current_duration) / file_size;
        }
    } else if (res == ERR_MP3_INDATA_UNDERFLOW) {
        if (fill_read_buffer() == 0) {
            audio_player_stop();
        }
    } else {
        // Error, skip byte
        read_ptr++;
        bytes_left--;
    }
    
    if (bytes_left < 1024) {
        fill_read_buffer();
    }
}

// Stubs for other functions
audio_player_state_t audio_player_get_state(void) { return player_state; }
uint32_t audio_player_get_position(void) { return current_position; }
uint32_t audio_player_get_duration(void) { return current_duration; }
bool audio_player_seek(uint32_t position) { return false; } // TODO
bool audio_player_set_mode(audio_player_mode_t mode) { player_mode = mode; return true; }
audio_player_mode_t audio_player_get_mode(void) { return player_mode; }
void audio_player_set_granular_speed(float speed) {}
void audio_player_set_granular_grain_size(float grain_size_ms) {}
void audio_player_set_granular_pitch(float pitch) {}
void audio_player_set_granular_jitter(float jitter) {}

