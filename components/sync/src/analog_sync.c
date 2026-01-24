/**
 * @file analog_sync.c
 * @brief Analog sync pulse implementation
 */

#include "analog_sync.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "analog_sync";

struct analog_sync_s {
    int gpio_pin;
    float bpm;
    float swing_ms;
    bool running;
    esp_timer_handle_t pulse_timer;
    bool pulse_state;
    uint32_t pulse_count;
};

static void analog_pulse_timer_callback(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    if (!sync->running) return;
    
    // Toggle pulse (5V high pulse)
    sync->pulse_state = !sync->pulse_state;
    gpio_set_level(sync->gpio_pin, sync->pulse_state ? 1 : 0);
    
    // Calculate next pulse time
    // 2 pulses per beat (8th notes)
    double period_us = (60.0 / sync->bpm / 2.0) * 1000000.0;
    
    // Apply swing to every second pulse
    if (sync->pulse_count % 2 == 1 && sync->swing_ms > 0.0f) {
        period_us += (sync->swing_ms * 1000.0);
    }
    
    sync->pulse_count++;
    
    // Restart timer with new period
    esp_timer_stop(sync->pulse_timer);
    esp_timer_start_once(sync->pulse_timer, (uint64_t)period_us);
}

analog_sync_t *analog_sync_create(int gpio_pin) {
    analog_sync_t *sync = (analog_sync_t *)malloc(sizeof(analog_sync_t));
    if (!sync) return NULL;
    
    memset(sync, 0, sizeof(analog_sync_t));
    sync->gpio_pin = gpio_pin;
    sync->bpm = 120.0f;
    sync->swing_ms = 0.0f;
    sync->running = false;
    sync->pulse_state = false;
    sync->pulse_count = 0;
    
    // Configure GPIO
    gpio_reset_pin(gpio_pin);
    gpio_set_direction(gpio_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_pin, 0);
    
    // Create timer
    esp_timer_create_args_t timer_args = {
        .callback = analog_pulse_timer_callback,
        .arg = sync,
        .name = "analog_sync"
    };
    esp_timer_create(&timer_args, &sync->pulse_timer);
    
    ESP_LOGI(TAG, "Analog sync created on GPIO %d", gpio_pin);
    
    return sync;
}

void analog_sync_destroy(analog_sync_t *sync) {
    if (!sync) return;
    
    analog_sync_stop(sync);
    
    if (sync->pulse_timer) {
        esp_timer_delete(sync->pulse_timer);
    }
    
    free(sync);
}

void analog_sync_set_bpm(analog_sync_t *sync, float bpm) {
    if (!sync) return;
    sync->bpm = fmaxf(60.0f, fminf(180.0f, bpm));
}

void analog_sync_set_swing(analog_sync_t *sync, float swing_ms) {
    if (!sync) return;
    sync->swing_ms = fmaxf(0.0f, fminf(50.0f, swing_ms));
}

void analog_sync_start(analog_sync_t *sync) {
    if (!sync) return;
    
    sync->running = true;
    sync->pulse_count = 0;
    
    // Start first pulse immediately
    double period_us = (60.0 / sync->bpm / 2.0) * 1000000.0;
    esp_timer_start_once(sync->pulse_timer, (uint64_t)period_us);
    
    ESP_LOGI(TAG, "Analog sync started @ %.2f BPM", sync->bpm);
}

void analog_sync_stop(analog_sync_t *sync) {
    if (!sync) return;
    
    esp_timer_stop(sync->pulse_timer);
    gpio_set_level(sync->gpio_pin, 0);
    sync->running = false;
    
    ESP_LOGI(TAG, "Analog sync stopped");
}

