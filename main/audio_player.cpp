/**
 * @file audio_player.cpp
 * @brief Audio player implementation using libhelix-mp3 with dedicated task
 */

#include "audio_player.h"
#include "audio_output.h"
#include "storage.h"
#include "granular_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "mp3dec.h"
#include "id3_parser.h"

static const char *TAG = "audio_player";

// MP3 decoding buffer sizes
#define READ_BUFFER_SIZE (4 * 1024)
#define OUTPUT_BUFFER_SIZE (MAX_NSAMP * MAX_NGRAN * MAX_NCHAN * sizeof(short)) 

static audio_player_state_t player_state = AUDIO_PLAYER_STATE_STOPPED;
static audio_player_mode_t player_mode = AUDIO_PLAYER_MODE_SIMPLE;
static char current_filepath[512] = {0};
static char current_track_title[128] = {0};
static uint32_t current_position = 0;
static uint32_t current_duration = 0;
static float current_gain = 0.5f;

// Decoder state
static HMP3Decoder hMP3Decoder = 0;
static FILE *mp3_file = NULL;
static uint8_t *read_buffer = NULL;
static int16_t *output_buffer = NULL;
static int bytes_left = 0;
static uint8_t *read_ptr = NULL;
static uint32_t file_size = 0;

// Waveform data for UI
static uint8_t waveform_peaks[480] = {0};
static int waveform_peak_idx = 0;

// Task handling
static TaskHandle_t audio_task_handle = NULL;
static QueueHandle_t command_queue = NULL;

typedef enum {
    CMD_LOAD,
    CMD_PLAY,
    CMD_PAUSE,
    CMD_STOP,
    CMD_SEEK
} player_cmd_type_t;

typedef struct {
    player_cmd_type_t type;
    char filepath[256];
    uint32_t seek_pos;
} player_cmd_t;

static int fill_read_buffer(void) {
    if (!mp3_file) return 0;
    if (bytes_left > 0 && read_ptr != read_buffer) {
        memmove(read_buffer, read_ptr, bytes_left);
    }
    int bytes_to_read = READ_BUFFER_SIZE - bytes_left;
    if (bytes_to_read > 0) {
        size_t read = fread(read_buffer + bytes_left, 1, bytes_to_read, mp3_file);
        bytes_left += read;
        read_ptr = read_buffer;
        if (read == 0) return 0;
    }
    return bytes_left;
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
}

static bool internal_load(const char *filepath) {
    internal_stop();
    ESP_LOGI(TAG, "Task loading file: %s", filepath);
    
    mp3_file = fopen(filepath, "rb");
    if (!mp3_file) return false;
    
    fseek(mp3_file, 0, SEEK_END);
    file_size = ftell(mp3_file);
    fseek(mp3_file, 0, SEEK_SET);
    
    hMP3Decoder = MP3InitDecoder();
    if (!hMP3Decoder) {
        fclose(mp3_file);
        mp3_file = NULL;
        return false;
    }
    
    bytes_left = 0;
    read_ptr = read_buffer;
    fill_read_buffer();
    
    // Reset waveform history
    waveform_peak_idx = 0;
    memset(waveform_peaks, 0, sizeof(waveform_peaks));
    
    strncpy(current_filepath, filepath, sizeof(current_filepath) - 1);
    
    id3_tag_t tag;
    if (id3_parse_file(filepath, &tag) && tag.title[0] != '\0') {
        // Clean title (remove leading spaces/nulls)
        char *src = tag.title;
        while (*src == ' ' || *src == '\0') src++;
        strncpy(current_track_title, src, sizeof(current_track_title) - 1);
    } else {
        const char *filename = strrchr(filepath, '/');
        filename = filename ? filename + 1 : filepath;
        strncpy(current_track_title, filename, sizeof(current_track_title) - 1);
        char *ext = strrchr(current_track_title, '.');
        if (ext) *ext = '\0';
    }
    
    current_position = 0;
    current_duration = file_size / (128 * 1024 / 8); 
    return true;
}

static void audio_task(void *pvParameters) {
    ESP_LOGI(TAG, "Audio task started on Core 1");
    player_cmd_t cmd;
    
    while (1) {
        // Guaranteed yield to prevent watchdog trigger on CPU 1
        vTaskDelay(pdMS_TO_TICKS(1));

        // Check for commands
        if (xQueueReceive(command_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CMD_LOAD: internal_load(cmd.filepath); break;
                case CMD_PLAY: player_state = AUDIO_PLAYER_STATE_PLAYING; break;
                case CMD_PAUSE: player_state = AUDIO_PLAYER_STATE_PAUSED; break;
                case CMD_STOP: internal_stop(); break;
                default: break;
            }
        }
        
        if (player_state == AUDIO_PLAYER_STATE_PLAYING && hMP3Decoder && mp3_file) {
            int offset = MP3FindSyncWord(read_ptr, bytes_left);
            if (offset < 0) {
                if (fill_read_buffer() == 0) internal_stop();
                continue;
            }
            read_ptr += offset;
            bytes_left -= offset;
            
            int res = MP3Decode(hMP3Decoder, &read_ptr, &bytes_left, output_buffer, 0);
            if (res == ERR_MP3_NONE) {
                MP3FrameInfo frameInfo;
                MP3GetLastFrameInfo(hMP3Decoder, &frameInfo);
                size_t samples = frameInfo.outputSamps;
                
                int16_t peak = 0;
                for (size_t i = 0; i < samples; i++) {
                    int16_t val = abs(output_buffer[i]);
                    if (val > peak) peak = val;
                    output_buffer[i] = (int16_t)(output_buffer[i] * current_gain);
                }
                
                waveform_peaks[waveform_peak_idx] = (uint8_t)(peak >> 5);
                waveform_peak_idx = (waveform_peak_idx + 1) % 480;
                
                audio_output_write(output_buffer, samples);
                
                if (file_size > 0) {
                    current_position = (ftell(mp3_file) * current_duration) / file_size;
                }
            } else if (res == ERR_MP3_INDATA_UNDERFLOW) {
                if (fill_read_buffer() == 0) internal_stop();
            } else {
                read_ptr++;
                bytes_left--;
            }
            
            if (bytes_left < 1024) fill_read_buffer();
        }
    }
}

bool audio_player_init(void) {
    read_buffer = (uint8_t*)heap_caps_malloc(READ_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    output_buffer = (int16_t*)heap_caps_malloc(OUTPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    command_queue = xQueueCreate(10, sizeof(player_cmd_t));
    
    // Increased stack to 12KB
    xTaskCreatePinnedToCore(audio_task, "audio_task", 12288, NULL, 5, &audio_task_handle, 1);
    return true;
}

bool audio_player_load(const char *filepath) {
    player_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_LOAD;
    strncpy(cmd.filepath, filepath, sizeof(cmd.filepath)-1);
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
    return true;
}

bool audio_player_play(void) {
    player_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_PLAY;
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
    return true;
}

void audio_player_stop(void) {
    player_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_STOP;
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
}

void audio_player_pause(void) {
    player_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_PAUSE;
    xQueueSend(command_queue, &cmd, portMAX_DELAY);
}

void audio_player_resume(void) { audio_player_play(); }

void audio_player_update(void) { /* Logic moved to task */ }

const char* audio_player_get_track_title(void) { return current_track_title; }
void audio_player_set_gain(float gain) { current_gain = gain; }
audio_player_state_t audio_player_get_state(void) { return player_state; }
uint32_t audio_player_get_position(void) { return current_position; }
uint32_t audio_player_get_duration(void) { return current_duration; }
void audio_player_get_waveform(uint8_t *buffer, size_t size) {
    for (size_t i = 0; i < size && i < 480; i++) {
        buffer[i] = waveform_peaks[(waveform_peak_idx + i) % 480];
    }
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