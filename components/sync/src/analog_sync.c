/**
 * @file analog_sync.c
 * @brief DIN sync implementation at 24ppqn
 */

#include "analog_sync.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "analog_sync";

/** Pulse width in microseconds */
#define PULSE_WIDTH_US 5000

/** Minimum/maximum BPM */
#define MIN_BPM 30.0f
#define MAX_BPM 300.0f
#define DEFAULT_BPM 120.0f

/** Clock timeout (ms) */
#define CLOCK_TIMEOUT_MS 2000

/** Jitter filter size */
#define JITTER_FILTER_SIZE 8

/** ISR install flag */
static bool gpio_isr_installed = false;

/**
 * @brief Jitter filter for clock recovery
 */
typedef struct {
    uint64_t samples[JITTER_FILTER_SIZE];
    int write_index;
    int sample_count;
    uint64_t filtered_period_us;
} jitter_filter_t;

/**
 * @brief DIN sync internal state
 */
struct analog_sync_s {
    // Pin configuration
    int clock_out_pin;
    int clock_in_pin;
    int run_out_pin;
    int run_in_pin;
    
    // Mode and state
    analog_sync_mode_t mode;
    bool running;
    bool clock_locked;
    
    // Master clock generation
    float master_bpm;
    float swing_ms;
    esp_timer_handle_t clock_timer;
    esp_timer_handle_t pulse_off_timer;
    bool pulse_state;
    uint32_t pulse_count;
    
    // Slave clock recovery
    uint64_t last_pulse_time_us;
    float slave_bpm;
    float phase;
    uint32_t slave_pulse_count;
    jitter_filter_t jitter;
    esp_timer_handle_t timeout_timer;
    
    // Callbacks
    analog_sync_callback_t transport_callback;
    analog_sync_led_callback_t led_callback;
    void *callback_user_data;
    
    // LED state
    analog_sync_led_state_t led_state;
};

// Forward declarations
static void clock_output_timer_cb(void *arg);
static void pulse_off_timer_cb(void *arg);
static void clock_timeout_timer_cb(void *arg);
static void IRAM_ATTR clock_input_isr(void *arg);
static void IRAM_ATTR run_input_isr(void *arg);
static void jitter_filter_init(jitter_filter_t *filter);
static uint64_t jitter_filter_add(jitter_filter_t *filter, uint64_t period_us);
static void update_led_state(analog_sync_t *sync, analog_sync_led_state_t state);

// ============================================================================
// Creation and destruction
// ============================================================================

analog_sync_t *analog_sync_create(const analog_sync_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "NULL config");
        return NULL;
    }
    
    analog_sync_t *sync = (analog_sync_t *)calloc(1, sizeof(analog_sync_t));
    if (!sync) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }
    
    // Store pin configuration
    sync->clock_out_pin = config->clock_out_pin;
    sync->clock_in_pin = config->clock_in_pin;
    sync->run_out_pin = config->run_out_pin;
    sync->run_in_pin = config->run_in_pin;
    
    // Initialize state
    sync->mode = ANALOG_SYNC_MODE_OFF;
    sync->running = false;
    sync->clock_locked = false;
    sync->master_bpm = DEFAULT_BPM;
    sync->slave_bpm = DEFAULT_BPM;
    sync->swing_ms = 0.0f;
    sync->led_state = ANALOG_SYNC_LED_OFF;
    
    jitter_filter_init(&sync->jitter);
    
    // Configure output pins
    if (sync->clock_out_pin >= 0) {
        gpio_reset_pin(sync->clock_out_pin);
        gpio_set_direction(sync->clock_out_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(sync->clock_out_pin, 0);
    }
    
    if (sync->run_out_pin >= 0) {
        gpio_reset_pin(sync->run_out_pin);
        gpio_set_direction(sync->run_out_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(sync->run_out_pin, 0);
    }
    
    // Install GPIO ISR service if needed
    if (!gpio_isr_installed && (sync->clock_in_pin >= 0 || sync->run_in_pin >= 0)) {
        esp_err_t err = gpio_install_isr_service(0);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            gpio_isr_installed = true;
        }
    }
    
    // Configure input pins with interrupts
    if (sync->clock_in_pin >= 0) {
        gpio_reset_pin(sync->clock_in_pin);
        gpio_set_direction(sync->clock_in_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(sync->clock_in_pin, GPIO_PULLDOWN_ONLY);
        gpio_set_intr_type(sync->clock_in_pin, GPIO_INTR_POSEDGE);
        gpio_isr_handler_add(sync->clock_in_pin, clock_input_isr, sync);
        gpio_intr_disable(sync->clock_in_pin);
    }
    
    if (sync->run_in_pin >= 0) {
        gpio_reset_pin(sync->run_in_pin);
        gpio_set_direction(sync->run_in_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(sync->run_in_pin, GPIO_PULLDOWN_ONLY);
        gpio_set_intr_type(sync->run_in_pin, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(sync->run_in_pin, run_input_isr, sync);
        gpio_intr_disable(sync->run_in_pin);
    }
    
    // Create timers
    esp_timer_create_args_t clock_timer_args = {
        .callback = clock_output_timer_cb,
        .arg = sync,
        .name = "din_clock"
    };
    esp_timer_create(&clock_timer_args, &sync->clock_timer);
    
    esp_timer_create_args_t pulse_off_args = {
        .callback = pulse_off_timer_cb,
        .arg = sync,
        .name = "din_pulse_off"
    };
    esp_timer_create(&pulse_off_args, &sync->pulse_off_timer);
    
    esp_timer_create_args_t timeout_args = {
        .callback = clock_timeout_timer_cb,
        .arg = sync,
        .name = "din_timeout"
    };
    esp_timer_create(&timeout_args, &sync->timeout_timer);
    
    ESP_LOGI(TAG, "DIN sync created (clk_out=%d, clk_in=%d, run_out=%d, run_in=%d)",
             sync->clock_out_pin, sync->clock_in_pin, 
             sync->run_out_pin, sync->run_in_pin);
    
    return sync;
}

analog_sync_t *analog_sync_create_simple(int gpio_pin) {
    analog_sync_config_t config = {
        .clock_out_pin = gpio_pin,
        .clock_in_pin = -1,
        .run_out_pin = -1,
        .run_in_pin = -1
    };
    return analog_sync_create(&config);
}

void analog_sync_destroy(analog_sync_t *sync) {
    if (!sync) return;
    
    analog_sync_stop(sync);
    analog_sync_set_mode(sync, ANALOG_SYNC_MODE_OFF);
    
    // Stop and delete timers
    if (sync->clock_timer) {
        esp_timer_stop(sync->clock_timer);
        esp_timer_delete(sync->clock_timer);
    }
    if (sync->pulse_off_timer) {
        esp_timer_stop(sync->pulse_off_timer);
        esp_timer_delete(sync->pulse_off_timer);
    }
    if (sync->timeout_timer) {
        esp_timer_stop(sync->timeout_timer);
        esp_timer_delete(sync->timeout_timer);
    }
    
    // Remove ISR handlers
    if (sync->clock_in_pin >= 0) {
        gpio_isr_handler_remove(sync->clock_in_pin);
    }
    if (sync->run_in_pin >= 0) {
        gpio_isr_handler_remove(sync->run_in_pin);
    }
    
    ESP_LOGI(TAG, "DIN sync destroyed");
    free(sync);
}

// ============================================================================
// Mode and configuration
// ============================================================================

void analog_sync_set_mode(analog_sync_t *sync, analog_sync_mode_t mode) {
    if (!sync) return;
    
    if (sync->running) {
        analog_sync_stop(sync);
    }
    
    // Disable interrupts for previous mode
    if (sync->mode == ANALOG_SYNC_MODE_SLAVE) {
        if (sync->clock_in_pin >= 0) {
            gpio_intr_disable(sync->clock_in_pin);
        }
        if (sync->run_in_pin >= 0) {
            gpio_intr_disable(sync->run_in_pin);
        }
        esp_timer_stop(sync->timeout_timer);
    }
    
    sync->mode = mode;
    sync->clock_locked = false;
    
    // Enable interrupts for slave mode
    if (mode == ANALOG_SYNC_MODE_SLAVE) {
        jitter_filter_init(&sync->jitter);
        sync->last_pulse_time_us = 0;
        sync->slave_pulse_count = 0;
        
        if (sync->clock_in_pin >= 0) {
            gpio_intr_enable(sync->clock_in_pin);
        }
        if (sync->run_in_pin >= 0) {
            gpio_intr_enable(sync->run_in_pin);
        }
        
        update_led_state(sync, ANALOG_SYNC_LED_WAITING);
    } else if (mode == ANALOG_SYNC_MODE_OFF) {
        update_led_state(sync, ANALOG_SYNC_LED_OFF);
    } else {
        update_led_state(sync, ANALOG_SYNC_LED_STOPPED);
    }
    
    ESP_LOGI(TAG, "DIN sync mode set to %d", mode);
}

analog_sync_mode_t analog_sync_get_mode(const analog_sync_t *sync) {
    return sync ? sync->mode : ANALOG_SYNC_MODE_OFF;
}

void analog_sync_set_bpm(analog_sync_t *sync, float bpm) {
    if (!sync) return;
    
    sync->master_bpm = fmaxf(MIN_BPM, fminf(MAX_BPM, bpm));
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER && sync->running) {
        uint64_t period_us = (uint64_t)((60.0 / sync->master_bpm / ANALOG_SYNC_PPQN) * 1000000.0);
        esp_timer_stop(sync->clock_timer);
        esp_timer_start_periodic(sync->clock_timer, period_us);
    }
}

float analog_sync_get_bpm(const analog_sync_t *sync) {
    if (!sync) return DEFAULT_BPM;
    
    if (sync->mode == ANALOG_SYNC_MODE_SLAVE && sync->clock_locked) {
        return sync->slave_bpm;
    }
    return sync->master_bpm;
}

void analog_sync_set_swing(analog_sync_t *sync, float swing_ms) {
    if (!sync) return;
    sync->swing_ms = fmaxf(0.0f, fminf(50.0f, swing_ms));
}

// ============================================================================
// Transport control
// ============================================================================

void analog_sync_start(analog_sync_t *sync) {
    if (!sync || sync->mode == ANALOG_SYNC_MODE_OFF) return;
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        sync->running = true;
        sync->pulse_count = 0;
        sync->phase = 0.0f;
        
        if (sync->run_out_pin >= 0) {
            gpio_set_level(sync->run_out_pin, 1);
        }
        
        // First pulse immediately
        if (sync->clock_out_pin >= 0) {
            gpio_set_level(sync->clock_out_pin, 1);
            sync->pulse_state = true;
            esp_timer_start_once(sync->pulse_off_timer, PULSE_WIDTH_US);
        }
        
        uint64_t period_us = (uint64_t)((60.0 / sync->master_bpm / ANALOG_SYNC_PPQN) * 1000000.0);
        esp_timer_start_periodic(sync->clock_timer, period_us);
        
        update_led_state(sync, ANALOG_SYNC_LED_RUNNING);
        ESP_LOGI(TAG, "DIN sync master started @ %.2f BPM", sync->master_bpm);
    }
    
    if (sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_START, sync->callback_user_data);
    }
}

void analog_sync_stop(analog_sync_t *sync) {
    if (!sync) return;
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        esp_timer_stop(sync->clock_timer);
        esp_timer_stop(sync->pulse_off_timer);
        
        if (sync->clock_out_pin >= 0) {
            gpio_set_level(sync->clock_out_pin, 0);
        }
        if (sync->run_out_pin >= 0) {
            gpio_set_level(sync->run_out_pin, 0);
        }
        
        sync->running = false;
        sync->pulse_state = false;
        
        update_led_state(sync, ANALOG_SYNC_LED_STOPPED);
        ESP_LOGI(TAG, "DIN sync master stopped");
    }
    
    if (sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_STOP, sync->callback_user_data);
    }
}

void analog_sync_continue(analog_sync_t *sync) {
    if (!sync || sync->mode != ANALOG_SYNC_MODE_MASTER) return;
    
    sync->running = true;
    
    if (sync->run_out_pin >= 0) {
        gpio_set_level(sync->run_out_pin, 1);
    }
    
    uint64_t period_us = (uint64_t)((60.0 / sync->master_bpm / ANALOG_SYNC_PPQN) * 1000000.0);
    esp_timer_start_periodic(sync->clock_timer, period_us);
    
    update_led_state(sync, ANALOG_SYNC_LED_RUNNING);
    ESP_LOGI(TAG, "DIN sync master continued");
    
    if (sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_CONTINUE, sync->callback_user_data);
    }
}

bool analog_sync_is_running(const analog_sync_t *sync) {
    return sync ? sync->running : false;
}

bool analog_sync_is_locked(const analog_sync_t *sync) {
    return sync ? sync->clock_locked : false;
}

float analog_sync_get_phase(const analog_sync_t *sync) {
    return sync ? sync->phase : 0.0f;
}

uint32_t analog_sync_get_pulse_count(const analog_sync_t *sync) {
    if (!sync) return 0;
    return (sync->mode == ANALOG_SYNC_MODE_MASTER) ? 
           sync->pulse_count : sync->slave_pulse_count;
}

void analog_sync_reset_phase(analog_sync_t *sync) {
    if (!sync) return;
    
    sync->phase = 0.0f;
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        sync->pulse_count = 0;
    } else {
        sync->slave_pulse_count = 0;
    }
}

// ============================================================================
// Callbacks
// ============================================================================

void analog_sync_set_callback(analog_sync_t *sync, 
                               analog_sync_callback_t callback,
                               void *user_data) {
    if (!sync) return;
    sync->transport_callback = callback;
    sync->callback_user_data = user_data;
}

void analog_sync_set_led_callback(analog_sync_t *sync,
                                   analog_sync_led_callback_t callback,
                                   void *user_data) {
    if (!sync) return;
    sync->led_callback = callback;
    if (!sync->callback_user_data) {
        sync->callback_user_data = user_data;
    }
}

analog_sync_led_state_t analog_sync_get_led_state(const analog_sync_t *sync) {
    return sync ? sync->led_state : ANALOG_SYNC_LED_OFF;
}

// ============================================================================
// Timer callbacks (master mode)
// ============================================================================

static void clock_output_timer_cb(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    if (!sync->running || sync->mode != ANALOG_SYNC_MODE_MASTER) {
        return;
    }
    
    if (sync->clock_out_pin >= 0) {
        gpio_set_level(sync->clock_out_pin, 1);
        sync->pulse_state = true;
        esp_timer_start_once(sync->pulse_off_timer, PULSE_WIDTH_US);
    }
    
    sync->pulse_count++;
    sync->phase = (float)(sync->pulse_count % ANALOG_SYNC_PPQN) / (float)ANALOG_SYNC_PPQN;
    
    if ((sync->pulse_count % ANALOG_SYNC_PPQN) == 0 && sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_BEAT, sync->callback_user_data);
    }
}

static void pulse_off_timer_cb(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    if (sync && sync->clock_out_pin >= 0) {
        gpio_set_level(sync->clock_out_pin, 0);
        sync->pulse_state = false;
    }
}

static void clock_timeout_timer_cb(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    if (sync->mode != ANALOG_SYNC_MODE_SLAVE) return;
    
    if (sync->clock_locked) {
        sync->clock_locked = false;
        sync->running = false;
        
        update_led_state(sync, ANALOG_SYNC_LED_WAITING);
        ESP_LOGW(TAG, "DIN sync clock lost");
        
        if (sync->transport_callback) {
            sync->transport_callback(sync, ANALOG_SYNC_EVENT_CLOCK_LOST, 
                                     sync->callback_user_data);
        }
    }
}

// ============================================================================
// ISR handlers (slave mode)
// ============================================================================

static void IRAM_ATTR clock_input_isr(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    uint64_t now_us = esp_timer_get_time();
    
    if (sync->last_pulse_time_us > 0) {
        uint64_t delta_us = now_us - sync->last_pulse_time_us;
        
        if (delta_us > 1000 && delta_us < 2000000) {
            uint64_t filtered_us = jitter_filter_add(&sync->jitter, delta_us);
            
            if (filtered_us > 0) {
                float period_s = (float)filtered_us / 1000000.0f;
                float new_bpm = 60.0f / (period_s * ANALOG_SYNC_PPQN);
                
                new_bpm = fmaxf(MIN_BPM, fminf(MAX_BPM, new_bpm));
                
                if (sync->clock_locked) {
                    sync->slave_bpm = sync->slave_bpm * 0.9f + new_bpm * 0.1f;
                } else {
                    sync->slave_bpm = new_bpm;
                }
                
                if (!sync->clock_locked && sync->jitter.sample_count >= 4) {
                    sync->clock_locked = true;
                    sync->running = true;
                }
            }
        }
    }
    
    sync->last_pulse_time_us = now_us;
    sync->slave_pulse_count++;
    sync->phase = (float)(sync->slave_pulse_count % ANALOG_SYNC_PPQN) / (float)ANALOG_SYNC_PPQN;
}

static void IRAM_ATTR run_input_isr(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    int level = gpio_get_level(sync->run_in_pin);
    
    if (level) {
        sync->running = true;
        sync->slave_pulse_count = 0;
        sync->phase = 0.0f;
    } else {
        sync->running = false;
    }
}

// ============================================================================
// Jitter filter implementation
// ============================================================================

static void jitter_filter_init(jitter_filter_t *filter) {
    memset(filter, 0, sizeof(jitter_filter_t));
}

static uint64_t jitter_filter_add(jitter_filter_t *filter, uint64_t period_us) {
    filter->samples[filter->write_index] = period_us;
    filter->write_index = (filter->write_index + 1) % JITTER_FILTER_SIZE;
    
    if (filter->sample_count < JITTER_FILTER_SIZE) {
        filter->sample_count++;
    }
    
    if (filter->sample_count < 2) {
        return 0;
    }
    
    // Calculate median
    uint64_t sorted[JITTER_FILTER_SIZE];
    int n = filter->sample_count;
    
    for (int i = 0; i < n; i++) {
        sorted[i] = filter->samples[i];
    }
    
    // Simple bubble sort
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (sorted[j] > sorted[j + 1]) {
                uint64_t tmp = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = tmp;
            }
        }
    }
    
    filter->filtered_period_us = sorted[n / 2];
    return filter->filtered_period_us;
}

// ============================================================================
// LED state management
// ============================================================================

static void update_led_state(analog_sync_t *sync, analog_sync_led_state_t state) {
    if (!sync) return;
    
    sync->led_state = state;
    
    if (sync->led_callback) {
        sync->led_callback(sync, state, sync->callback_user_data);
    }
}

// ============================================================================
// Sync manager integration
// ============================================================================

void analog_sync_tick(analog_sync_t *sync) {
    if (!sync) return;
    
    if (sync->mode == ANALOG_SYNC_MODE_SLAVE) {
        if (sync->clock_locked && sync->running) {
            if (sync->led_state != ANALOG_SYNC_LED_RUNNING) {
                update_led_state(sync, ANALOG_SYNC_LED_RUNNING);
            }
        } else if (sync->clock_locked) {
            if (sync->led_state != ANALOG_SYNC_LED_LOCKED) {
                update_led_state(sync, ANALOG_SYNC_LED_LOCKED);
            }
        }
        
        esp_timer_stop(sync->timeout_timer);
        if (sync->clock_locked) {
            esp_timer_start_once(sync->timeout_timer, CLOCK_TIMEOUT_MS * 1000);
        }
    }
}
