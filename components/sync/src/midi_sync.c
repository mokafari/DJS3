/**
 * @file midi_sync.c
 * @brief MIDI clock synchronization implementation
 */

#include "midi_sync.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "midi_sync";

#define MIDI_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_CONTINUE 0xFB

#define MIDI_CLOCKS_PER_BEAT 24

struct midi_sync_s {
    int uart_num;
    int tx_pin;
    midi_sync_mode_t mode;
    float bpm;
    bool running;
    
    // Master mode
    esp_timer_handle_t clock_timer;
    uint32_t clock_counter;
    
    // Slave mode
    uint32_t last_clock_time_us;
    float slave_bpm;
    float phase;
    uint32_t clock_count;
};

static void midi_clock_timer_callback(void *arg) {
    midi_sync_t *sync = (midi_sync_t *)arg;
    
    if (sync->mode != MIDI_SYNC_MODE_MASTER || !sync->running) {
        return;
    }
    
    // Send MIDI clock byte
    uint8_t clock_byte = MIDI_CLOCK;
    uart_write_bytes(sync->uart_num, &clock_byte, 1);
    
    sync->clock_counter++;
}

midi_sync_t *midi_sync_create(int uart_num, int tx_pin) {
    midi_sync_t *sync = (midi_sync_t *)malloc(sizeof(midi_sync_t));
    if (!sync) return NULL;
    
    memset(sync, 0, sizeof(midi_sync_t));
    sync->uart_num = uart_num;
    sync->tx_pin = tx_pin;
    sync->mode = MIDI_SYNC_MODE_OFF;
    sync->bpm = 120.0f;
    sync->running = false;
    
    // Configure UART for MIDI (31250 baud)
    uart_config_t uart_config = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, UART_PIN_NO_CHANGE, 
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, 256, 0, 0, NULL, 0);
    
    // Create timer for MIDI clock (master mode)
    esp_timer_create_args_t timer_args = {
        .callback = midi_clock_timer_callback,
        .arg = sync,
        .name = "midi_clock"
    };
    esp_timer_create(&timer_args, &sync->clock_timer);
    
    ESP_LOGI(TAG, "MIDI sync created on UART %d, TX pin %d", uart_num, tx_pin);
    
    return sync;
}

void midi_sync_destroy(midi_sync_t *sync) {
    if (!sync) return;
    
    if (sync->clock_timer) {
        esp_timer_stop(sync->clock_timer);
        esp_timer_delete(sync->clock_timer);
    }
    
    uart_driver_delete(sync->uart_num);
    free(sync);
}

void midi_sync_set_mode(midi_sync_t *sync, midi_sync_mode_t mode) {
    if (!sync) return;
    
    if (sync->running && sync->mode == MIDI_SYNC_MODE_MASTER) {
        midi_sync_stop(sync);
    }
    
    sync->mode = mode;
    ESP_LOGI(TAG, "MIDI sync mode set to %d", mode);
}

void midi_sync_set_bpm(midi_sync_t *sync, float bpm) {
    if (!sync) return;
    
    sync->bpm = fmaxf(60.0f, fminf(180.0f, bpm));
    
    // Update timer period if running in master mode
    if (sync->mode == MIDI_SYNC_MODE_MASTER && sync->running) {
        // Calculate microseconds per MIDI clock tick
        // 24 clocks per beat, so: (60 / bpm) / 24 seconds per clock
        double period_us = (60.0 / sync->bpm / MIDI_CLOCKS_PER_BEAT) * 1000000.0;
        esp_timer_stop(sync->clock_timer);
        esp_timer_start_periodic(sync->clock_timer, (uint64_t)period_us);
    }
    
    ESP_LOGI(TAG, "MIDI sync BPM set to %.2f", sync->bpm);
}

float midi_sync_get_bpm(const midi_sync_t *sync) {
    if (!sync) return 120.0f;
    
    if (sync->mode == MIDI_SYNC_MODE_SLAVE) {
        return sync->slave_bpm;
    }
    
    return sync->bpm;
}

void midi_sync_start(midi_sync_t *sync) {
    if (!sync || sync->mode != MIDI_SYNC_MODE_MASTER) return;
    
    // Send MIDI Start message
    uint8_t start_byte = MIDI_START;
    uart_write_bytes(sync->uart_num, &start_byte, 1);
    
    // Calculate timer period
    double period_us = (60.0 / sync->bpm / MIDI_CLOCKS_PER_BEAT) * 1000000.0;
    esp_timer_start_periodic(sync->clock_timer, (uint64_t)period_us);
    
    sync->running = true;
    sync->clock_counter = 0;
    
    ESP_LOGI(TAG, "MIDI sync started @ %.2f BPM", sync->bpm);
}

void midi_sync_stop(midi_sync_t *sync) {
    if (!sync || sync->mode != MIDI_SYNC_MODE_MASTER) return;
    
    esp_timer_stop(sync->clock_timer);
    
    // Send MIDI Stop message
    uint8_t stop_byte = MIDI_STOP;
    uart_write_bytes(sync->uart_num, &stop_byte, 1);
    
    sync->running = false;
    ESP_LOGI(TAG, "MIDI sync stopped");
}

void midi_sync_process_byte(midi_sync_t *sync, uint8_t data) {
    if (!sync || sync->mode != MIDI_SYNC_MODE_SLAVE) return;
    
    uint64_t now_us = esp_timer_get_time();
    
    switch (data) {
        case MIDI_START:
            sync->running = true;
            sync->clock_count = 0;
            sync->last_clock_time_us = now_us;
            ESP_LOGI(TAG, "MIDI Start received");
            break;
            
        case MIDI_STOP:
            sync->running = false;
            ESP_LOGI(TAG, "MIDI Stop received");
            break;
            
        case MIDI_CONTINUE:
            sync->running = true;
            sync->last_clock_time_us = now_us;
            ESP_LOGI(TAG, "MIDI Continue received");
            break;
            
        case MIDI_CLOCK:
            if (sync->running && sync->last_clock_time_us > 0) {
                uint64_t delta_us = now_us - sync->last_clock_time_us;
                
                // Calculate BPM from clock interval
                // 24 clocks per beat, so: 60 / (delta_us / 1e6 * 24)
                if (delta_us > 0) {
                    sync->slave_bpm = 60.0f / ((float)delta_us / 1000000.0f * MIDI_CLOCKS_PER_BEAT);
                    sync->slave_bpm = fmaxf(60.0f, fminf(180.0f, sync->slave_bpm));
                }
                
                // Calculate phase (0.0 to 1.0)
                sync->phase = (float)(sync->clock_count % MIDI_CLOCKS_PER_BEAT) / (float)MIDI_CLOCKS_PER_BEAT;
            }
            
            sync->last_clock_time_us = now_us;
            sync->clock_count++;
            break;
    }
}

bool midi_sync_is_running(const midi_sync_t *sync) {
    if (!sync) return false;
    return sync->running;
}

float midi_sync_get_phase(const midi_sync_t *sync) {
    if (!sync) return 0.0f;
    return sync->phase;
}

