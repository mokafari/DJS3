/**
 * @file analog_sync.c
 * @brief DIN sync implementation at 24ppqn
 * 
 * Full DIN sync protocol supporting:
 * - Master mode with precise pulse generation
 * - Slave mode with jitter-filtered clock recovery
 * - Swing timing for groove
 * - Sub-pulse phase interpolation
 */

#include "analog_sync.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "analog_sync";

// ============================================================================
// Constants
// ============================================================================

/** Pulse width in microseconds (5ms is standard for DIN sync) */
#define PULSE_WIDTH_US 5000

/** Minimum/maximum BPM limits */
#define MIN_BPM 30.0f
#define MAX_BPM 300.0f
#define DEFAULT_BPM 120.0f

/** Clock timeout for slave mode (ms) */
#define CLOCK_TIMEOUT_MS 2000

/** Jitter filter size (must be power of 2 for efficiency) */
#define JITTER_FILTER_SIZE 8
#define JITTER_FILTER_MASK (JITTER_FILTER_SIZE - 1)

/** Minimum samples before declaring clock lock */
#define MIN_LOCK_SAMPLES 4

/** Swing quantization: every 6 pulses (16th note at 24ppqn) */
#define SWING_PULSE_INTERVAL 6

/** Default quantum (beats per bar) */
#define DEFAULT_QUANTUM 4.0f

/** PLL bandwidth coefficients (alpha for IIR filter) */
#define PLL_ALPHA_TIGHT  0.3f   // Fast tracking, more jitter
#define PLL_ALPHA_NORMAL 0.1f   // Balanced (default)
#define PLL_ALPHA_SMOOTH 0.03f  // Slow tracking, stable tempo

/** Drift measurement window */
#define DRIFT_WINDOW_SIZE 64
#define DRIFT_WINDOW_MASK (DRIFT_WINDOW_SIZE - 1)

/** Parts per billion/million conversion */
#define PPB_PER_US_PER_PERIOD(drift_us, period_us) \
    ((int64_t)(drift_us) * 1000000000LL / (int64_t)(period_us))

/** ISR install flag */
static bool s_gpio_isr_installed = false;

// ============================================================================
// Internal types
// ============================================================================

/**
 * @brief Jitter filter for clock recovery
 * 
 * Uses median filtering to reject outliers and smooth period estimates.
 */
typedef struct {
    uint64_t samples[JITTER_FILTER_SIZE];
    uint8_t write_index;
    uint8_t sample_count;
    uint64_t filtered_period_us;
} jitter_filter_t;

/**
 * @brief PLL state for drift correction
 * 
 * Software Phase-Locked Loop that tracks external clock while
 * filtering jitter and smoothing tempo changes.
 */
typedef struct {
    double phase;              // Current phase accumulator (0.0 to 1.0)
    double frequency;          // Estimated frequency (Hz)
    double phase_error;        // Last measured phase error
    double freq_error;         // Integrated frequency error
    float alpha;               // Loop bandwidth (filter coefficient)
    float beta;                // Frequency correction gain
    int64_t last_update_us;    // Time of last update
    bool initialized;          // Has received first sample
} pll_state_t;

/**
 * @brief Drift measurement statistics
 */
typedef struct {
    int64_t period_samples[DRIFT_WINDOW_SIZE];  // Period measurements
    uint32_t write_index;
    uint32_t sample_count;
    int64_t sum_period_us;         // Running sum for mean
    int64_t sum_sq_diff;           // For variance calculation
    int64_t expected_period_us;    // Nominal period at current BPM
    int64_t lock_time_us;          // When clock locked
    uint32_t glitch_count;         // Rejected outliers
    float drift_ppb;               // Calculated drift
} drift_tracker_t;

/**
 * @brief Pending events from ISR (processed in tick)
 */
typedef struct {
    uint8_t transport_start : 1;
    uint8_t transport_stop : 1;
    uint8_t clock_lost : 1;
    uint8_t beat : 1;
    uint8_t reserved : 4;
} pending_events_t;

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
    volatile bool running;
    volatile bool clock_locked;
    
    // Master clock generation
    float master_bpm;
    float swing_ms;
    esp_timer_handle_t clock_timer;
    esp_timer_handle_t pulse_off_timer;
    volatile bool pulse_state;
    volatile uint32_t pulse_count;
    uint32_t swing_pulse_counter;  // For tracking swing timing
    
    // Slave clock recovery
    volatile uint64_t last_pulse_time_us;
    volatile uint64_t current_period_us;
    volatile float slave_bpm;
    volatile float phase;
    volatile uint32_t slave_pulse_count;
    jitter_filter_t jitter;
    esp_timer_handle_t timeout_timer;
    
    // PLL-based drift correction
    pll_state_t pll;
    drift_tracker_t drift;
    analog_sync_pll_bandwidth_t pll_bandwidth;
    
    // Quantum/bar alignment
    float quantum;              // Beats per bar (default 4.0)
    volatile double beat_position;  // Fractional beat count
    volatile double bar_phase;      // Phase within quantum
    
    // Reference timestamps for time calculations
    volatile int64_t transport_start_time_us;
    volatile int64_t last_beat_time_us;
    
    // Pending events (ISR -> tick)
    volatile pending_events_t pending;
    portMUX_TYPE spinlock;
    
    // Callbacks
    analog_sync_callback_t transport_callback;
    analog_sync_led_callback_t led_callback;
    void *callback_user_data;
    
    // LED state
    analog_sync_led_state_t led_state;
    
    // Clock divider/multiplier
    analog_sync_clock_rate_t clock_rate;
    uint8_t clock_divider_counter;  // Counter for division
    int effective_ppqn;             // Cached effective PPQN
};

// ============================================================================
// Forward declarations
// ============================================================================

static void clock_output_timer_cb(void *arg);
static void pulse_off_timer_cb(void *arg);
static void clock_timeout_timer_cb(void *arg);
static void IRAM_ATTR clock_input_isr(void *arg);
static void IRAM_ATTR run_input_isr(void *arg);
static void jitter_filter_init(jitter_filter_t *filter);
static uint64_t jitter_filter_add(jitter_filter_t *filter, uint64_t period_us);
static void update_led_state(analog_sync_t *sync, analog_sync_led_state_t state);
static uint64_t bpm_to_period_us(float bpm);
static float period_us_to_bpm(uint64_t period_us);

// PLL and drift correction
static void pll_init(pll_state_t *pll, float alpha);
static void pll_update(pll_state_t *pll, int64_t measured_time_us, int64_t expected_period_us);
static float pll_get_corrected_bpm(const pll_state_t *pll, float nominal_bpm);
static void drift_tracker_init(drift_tracker_t *drift);
static void drift_tracker_add(drift_tracker_t *drift, int64_t period_us, int64_t expected_us);
static float get_pll_alpha(analog_sync_pll_bandwidth_t bandwidth);

// ============================================================================
// Utility functions
// ============================================================================

/**
 * @brief Convert BPM to pulse period in microseconds
 */
static uint64_t bpm_to_period_us(float bpm) {
    // Period = 60 seconds / (bpm * ppqn)
    // In microseconds: 60,000,000 / (bpm * 24)
    return (uint64_t)(60000000.0 / (bpm * ANALOG_SYNC_PPQN));
}

/**
 * @brief Convert pulse period to BPM (at standard 24 PPQN)
 */
static float period_us_to_bpm(uint64_t period_us) {
    if (period_us == 0) return DEFAULT_BPM;
    return 60000000.0f / ((float)period_us * ANALOG_SYNC_PPQN);
}

/**
 * @brief Convert pulse period to BPM with custom PPQN
 */
static float period_us_to_bpm_ppqn(uint64_t period_us, int ppqn) {
    if (period_us == 0 || ppqn == 0) return DEFAULT_BPM;
    return 60000000.0f / ((float)period_us * ppqn);
}

/**
 * @brief Clamp BPM to valid range
 */
static inline float clamp_bpm(float bpm) {
    if (bpm < MIN_BPM) return MIN_BPM;
    if (bpm > MAX_BPM) return MAX_BPM;
    return bpm;
}

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
    
    // Initialize spinlock
    portMUX_INITIALIZE(&sync->spinlock);
    
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
    sync->current_period_us = bpm_to_period_us(DEFAULT_BPM);
    
    // Initialize quantum and beat tracking
    sync->quantum = DEFAULT_QUANTUM;
    sync->beat_position = 0.0;
    sync->bar_phase = 0.0;
    sync->transport_start_time_us = 0;
    sync->last_beat_time_us = 0;
    
    // Initialize PLL and drift correction
    sync->pll_bandwidth = ANALOG_SYNC_PLL_NORMAL;
    pll_init(&sync->pll, get_pll_alpha(sync->pll_bandwidth));
    drift_tracker_init(&sync->drift);
    
    // Initialize clock rate (1x = standard 24 PPQN)
    sync->clock_rate = ANALOG_SYNC_CLOCK_1X;
    sync->clock_divider_counter = 0;
    sync->effective_ppqn = ANALOG_SYNC_PPQN;
    
    jitter_filter_init(&sync->jitter);
    
    // Configure output pins
    if (sync->clock_out_pin >= 0) {
        gpio_reset_pin(sync->clock_out_pin);
        gpio_set_direction(sync->clock_out_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(sync->clock_out_pin, 0);
        ESP_LOGD(TAG, "Clock output on GPIO %d", sync->clock_out_pin);
    }
    
    if (sync->run_out_pin >= 0) {
        gpio_reset_pin(sync->run_out_pin);
        gpio_set_direction(sync->run_out_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(sync->run_out_pin, 0);
        ESP_LOGD(TAG, "Run output on GPIO %d", sync->run_out_pin);
    }
    
    // Install GPIO ISR service if needed
    if (!s_gpio_isr_installed && (sync->clock_in_pin >= 0 || sync->run_in_pin >= 0)) {
        esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err == ESP_OK) {
            s_gpio_isr_installed = true;
            ESP_LOGD(TAG, "GPIO ISR service installed");
        } else if (err == ESP_ERR_INVALID_STATE) {
            // Already installed by another component
            s_gpio_isr_installed = true;
        } else {
            ESP_LOGW(TAG, "Failed to install GPIO ISR service: %d", err);
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
        ESP_LOGD(TAG, "Clock input on GPIO %d", sync->clock_in_pin);
    }
    
    if (sync->run_in_pin >= 0) {
        gpio_reset_pin(sync->run_in_pin);
        gpio_set_direction(sync->run_in_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(sync->run_in_pin, GPIO_PULLDOWN_ONLY);
        gpio_set_intr_type(sync->run_in_pin, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(sync->run_in_pin, run_input_isr, sync);
        gpio_intr_disable(sync->run_in_pin);
        ESP_LOGD(TAG, "Run input on GPIO %d", sync->run_in_pin);
    }
    
    // Create timers
    esp_timer_create_args_t clock_timer_args = {
        .callback = clock_output_timer_cb,
        .arg = sync,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "din_clock"
    };
    ESP_ERROR_CHECK(esp_timer_create(&clock_timer_args, &sync->clock_timer));
    
    esp_timer_create_args_t pulse_off_args = {
        .callback = pulse_off_timer_cb,
        .arg = sync,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "din_pulse_off"
    };
    ESP_ERROR_CHECK(esp_timer_create(&pulse_off_args, &sync->pulse_off_timer));
    
    esp_timer_create_args_t timeout_args = {
        .callback = clock_timeout_timer_cb,
        .arg = sync,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "din_timeout"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timeout_args, &sync->timeout_timer));
    
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
    
    // Stop everything first
    analog_sync_stop(sync);
    analog_sync_set_mode(sync, ANALOG_SYNC_MODE_OFF);
    
    // Stop and delete timers
    if (sync->clock_timer) {
        esp_timer_stop(sync->clock_timer);
        esp_timer_delete(sync->clock_timer);
        sync->clock_timer = NULL;
    }
    if (sync->pulse_off_timer) {
        esp_timer_stop(sync->pulse_off_timer);
        esp_timer_delete(sync->pulse_off_timer);
        sync->pulse_off_timer = NULL;
    }
    if (sync->timeout_timer) {
        esp_timer_stop(sync->timeout_timer);
        esp_timer_delete(sync->timeout_timer);
        sync->timeout_timer = NULL;
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
    
    // Stop if currently running
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
    
    // Clear pending events
    portENTER_CRITICAL(&sync->spinlock);
    memset((void *)&sync->pending, 0, sizeof(pending_events_t));
    portEXIT_CRITICAL(&sync->spinlock);
    
    // Enable interrupts for slave mode
    if (mode == ANALOG_SYNC_MODE_SLAVE) {
        jitter_filter_init(&sync->jitter);
        pll_init(&sync->pll, get_pll_alpha(sync->pll_bandwidth));
        drift_tracker_init(&sync->drift);
        sync->drift.lock_time_us = esp_timer_get_time();
        
        sync->last_pulse_time_us = 0;
        sync->slave_pulse_count = 0;
        sync->phase = 0.0f;
        sync->beat_position = 0.0;
        sync->bar_phase = 0.0;
        
        if (sync->clock_in_pin >= 0) {
            gpio_intr_enable(sync->clock_in_pin);
        }
        if (sync->run_in_pin >= 0) {
            gpio_intr_enable(sync->run_in_pin);
        }
        
        update_led_state(sync, ANALOG_SYNC_LED_WAITING);
        ESP_LOGI(TAG, "DIN sync slave mode enabled");
    } else if (mode == ANALOG_SYNC_MODE_MASTER) {
        sync->pulse_count = 0;
        sync->swing_pulse_counter = 0;
        sync->phase = 0.0f;
        
        update_led_state(sync, ANALOG_SYNC_LED_STOPPED);
        ESP_LOGI(TAG, "DIN sync master mode enabled @ %.2f BPM", sync->master_bpm);
    } else {
        update_led_state(sync, ANALOG_SYNC_LED_OFF);
        ESP_LOGI(TAG, "DIN sync disabled");
    }
}

analog_sync_mode_t analog_sync_get_mode(const analog_sync_t *sync) {
    return sync ? sync->mode : ANALOG_SYNC_MODE_OFF;
}

void analog_sync_set_bpm(analog_sync_t *sync, float bpm) {
    if (!sync) return;
    
    bpm = clamp_bpm(bpm);
    sync->master_bpm = bpm;
    
    // Update timer if running in master mode
    if (sync->mode == ANALOG_SYNC_MODE_MASTER && sync->running) {
        uint64_t base_period_us = bpm_to_period_us(bpm);
        uint64_t period_us = base_period_us;
        
        // Adjust for clock rate
        if (sync->clock_rate < 0) {
            period_us = base_period_us * (-sync->clock_rate);
        } else if (sync->clock_rate > 1) {
            period_us = base_period_us / sync->clock_rate;
        }
        
        esp_timer_stop(sync->clock_timer);
        esp_timer_start_periodic(sync->clock_timer, period_us);
        ESP_LOGD(TAG, "Master BPM updated to %.2f (period=%llu us)", bpm, period_us);
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
    
    // Clamp swing to reasonable range (0-50ms)
    if (swing_ms < 0.0f) swing_ms = 0.0f;
    if (swing_ms > 50.0f) swing_ms = 50.0f;
    
    sync->swing_ms = swing_ms;
    ESP_LOGD(TAG, "Swing set to %.2f ms", swing_ms);
}

float analog_sync_get_swing(const analog_sync_t *sync) {
    return sync ? sync->swing_ms : 0.0f;
}

// ============================================================================
// Clock divider/multiplier
// ============================================================================

/**
 * @brief Calculate effective PPQN from clock rate setting
 */
static int calculate_effective_ppqn(analog_sync_clock_rate_t rate) {
    if (rate < 0) {
        // Divider: negative values divide
        return ANALOG_SYNC_PPQN / (-rate);
    } else if (rate > 1) {
        // Multiplier: values > 1 multiply
        return ANALOG_SYNC_PPQN * rate;
    }
    return ANALOG_SYNC_PPQN;  // 1x = standard
}

void analog_sync_set_clock_rate(analog_sync_t *sync, analog_sync_clock_rate_t rate) {
    if (!sync) return;
    
    // Validate rate
    if (rate == 0 || rate < -4 || rate > 4) {
        ESP_LOGW(TAG, "Invalid clock rate %d, ignoring", rate);
        return;
    }
    
    sync->clock_rate = rate;
    sync->clock_divider_counter = 0;  // Reset counter on rate change
    sync->effective_ppqn = calculate_effective_ppqn(rate);
    
    const char *rate_str;
    switch (rate) {
        case ANALOG_SYNC_CLOCK_DIV_4: rate_str = "÷4 (6 PPQN)"; break;
        case ANALOG_SYNC_CLOCK_DIV_3: rate_str = "÷3 (8 PPQN)"; break;
        case ANALOG_SYNC_CLOCK_DIV_2: rate_str = "÷2 (12 PPQN)"; break;
        case ANALOG_SYNC_CLOCK_1X:    rate_str = "1x (24 PPQN)"; break;
        case ANALOG_SYNC_CLOCK_MUL_2: rate_str = "×2 (48 PPQN)"; break;
        case ANALOG_SYNC_CLOCK_MUL_3: rate_str = "×3 (72 PPQN)"; break;
        case ANALOG_SYNC_CLOCK_MUL_4: rate_str = "×4 (96 PPQN)"; break;
        default: rate_str = "unknown"; break;
    }
    
    ESP_LOGI(TAG, "Clock rate set to %s", rate_str);
    
    // If running in master mode, restart timer with new rate
    if (sync->mode == ANALOG_SYNC_MODE_MASTER && sync->running) {
        uint64_t base_period_us = bpm_to_period_us(sync->master_bpm);
        uint64_t adjusted_period_us;
        
        if (rate < 0) {
            // Divider: longer period (fewer pulses)
            adjusted_period_us = base_period_us * (-rate);
        } else if (rate > 1) {
            // Multiplier: shorter period (more pulses)
            adjusted_period_us = base_period_us / rate;
        } else {
            adjusted_period_us = base_period_us;
        }
        
        esp_timer_stop(sync->clock_timer);
        esp_timer_start_periodic(sync->clock_timer, adjusted_period_us);
        ESP_LOGD(TAG, "Timer period adjusted to %llu us", adjusted_period_us);
    }
}

analog_sync_clock_rate_t analog_sync_get_clock_rate(const analog_sync_t *sync) {
    return sync ? sync->clock_rate : ANALOG_SYNC_CLOCK_1X;
}

int analog_sync_get_effective_ppqn(const analog_sync_t *sync) {
    return sync ? sync->effective_ppqn : ANALOG_SYNC_PPQN;
}

// ============================================================================
// Transport control
// ============================================================================

void analog_sync_start(analog_sync_t *sync) {
    if (!sync || sync->mode == ANALOG_SYNC_MODE_OFF) return;
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        // Reset counters
        sync->pulse_count = 0;
        sync->swing_pulse_counter = 0;
        sync->phase = 0.0f;
        sync->beat_position = 0.0;
        sync->bar_phase = 0.0;
        sync->running = true;
        sync->transport_start_time_us = esp_timer_get_time();
        sync->last_beat_time_us = sync->transport_start_time_us;
        
        // Set RUN signal high
        if (sync->run_out_pin >= 0) {
            gpio_set_level(sync->run_out_pin, 1);
        }
        
        // Generate first pulse immediately
        if (sync->clock_out_pin >= 0) {
            gpio_set_level(sync->clock_out_pin, 1);
            sync->pulse_state = true;
            esp_timer_start_once(sync->pulse_off_timer, PULSE_WIDTH_US);
        }
        
        // Start periodic clock timer (adjusted for clock rate)
        uint64_t base_period_us = bpm_to_period_us(sync->master_bpm);
        uint64_t period_us = base_period_us;
        
        // Adjust period for clock divider/multiplier
        if (sync->clock_rate < 0) {
            // Divider: longer period (fewer pulses)
            period_us = base_period_us * (-sync->clock_rate);
        } else if (sync->clock_rate > 1) {
            // Multiplier: shorter period (more pulses)
            period_us = base_period_us / sync->clock_rate;
        }
        
        sync->clock_divider_counter = 0;
        esp_timer_start_periodic(sync->clock_timer, period_us);
        
        update_led_state(sync, ANALOG_SYNC_LED_RUNNING);
        ESP_LOGI(TAG, "DIN sync started @ %.2f BPM (period=%llu us, rate=%d)", 
                 sync->master_bpm, period_us, sync->clock_rate);
        
        // Fire start callback
        if (sync->transport_callback) {
            sync->transport_callback(sync, ANALOG_SYNC_EVENT_START, 
                                     sync->callback_user_data);
        }
    }
    // In slave mode, transport is controlled by external RUN signal
}

void analog_sync_stop(analog_sync_t *sync) {
    if (!sync) return;
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER && sync->running) {
        // Stop clock timer
        esp_timer_stop(sync->clock_timer);
        esp_timer_stop(sync->pulse_off_timer);
        
        // Ensure outputs are low
        if (sync->clock_out_pin >= 0) {
            gpio_set_level(sync->clock_out_pin, 0);
        }
        if (sync->run_out_pin >= 0) {
            gpio_set_level(sync->run_out_pin, 0);
        }
        
        sync->running = false;
        sync->pulse_state = false;
        
        update_led_state(sync, ANALOG_SYNC_LED_STOPPED);
        ESP_LOGI(TAG, "DIN sync stopped");
        
        // Fire stop callback
        if (sync->transport_callback) {
            sync->transport_callback(sync, ANALOG_SYNC_EVENT_STOP, 
                                     sync->callback_user_data);
        }
    }
}

void analog_sync_continue(analog_sync_t *sync) {
    if (!sync || sync->mode != ANALOG_SYNC_MODE_MASTER) return;
    
    if (!sync->running) {
        sync->running = true;
        
        // Set RUN signal high (don't reset counters)
        if (sync->run_out_pin >= 0) {
            gpio_set_level(sync->run_out_pin, 1);
        }
        
        // Resume clock timer (adjusted for clock rate)
        uint64_t base_period_us = bpm_to_period_us(sync->master_bpm);
        uint64_t period_us = base_period_us;
        
        if (sync->clock_rate < 0) {
            period_us = base_period_us * (-sync->clock_rate);
        } else if (sync->clock_rate > 1) {
            period_us = base_period_us / sync->clock_rate;
        }
        
        esp_timer_start_periodic(sync->clock_timer, period_us);
        
        update_led_state(sync, ANALOG_SYNC_LED_RUNNING);
        ESP_LOGI(TAG, "DIN sync continued @ pulse %lu", sync->pulse_count);
        
        // Fire continue callback
        if (sync->transport_callback) {
            sync->transport_callback(sync, ANALOG_SYNC_EVENT_CONTINUE, 
                                     sync->callback_user_data);
        }
    }
}

bool analog_sync_is_running(const analog_sync_t *sync) {
    return sync ? sync->running : false;
}

bool analog_sync_is_locked(const analog_sync_t *sync) {
    return sync ? sync->clock_locked : false;
}

float analog_sync_get_phase(const analog_sync_t *sync) {
    if (!sync) return 0.0f;
    
    // For more accurate phase in slave mode, interpolate based on time since last pulse
    if (sync->mode == ANALOG_SYNC_MODE_SLAVE && sync->clock_locked && sync->running) {
        uint64_t now_us = esp_timer_get_time();
        uint64_t elapsed_us = now_us - sync->last_pulse_time_us;
        uint64_t period_us = sync->current_period_us;
        
        if (period_us > 0 && elapsed_us < period_us * 2) {
            // Calculate sub-pulse phase
            uint32_t pulse_in_beat = sync->slave_pulse_count % ANALOG_SYNC_PPQN;
            float base_phase = (float)pulse_in_beat / (float)ANALOG_SYNC_PPQN;
            float sub_phase = (float)elapsed_us / (float)period_us / (float)ANALOG_SYNC_PPQN;
            return fmodf(base_phase + sub_phase, 1.0f);
        }
    }
    
    return sync->phase;
}

uint32_t analog_sync_get_pulse_count(const analog_sync_t *sync) {
    if (!sync) return 0;
    return (sync->mode == ANALOG_SYNC_MODE_MASTER) ? 
           sync->pulse_count : sync->slave_pulse_count;
}

void analog_sync_reset_phase(analog_sync_t *sync) {
    if (!sync) return;
    
    portENTER_CRITICAL(&sync->spinlock);
    sync->phase = 0.0f;
    sync->clock_divider_counter = 0;
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        sync->pulse_count = 0;
        sync->swing_pulse_counter = 0;
    } else {
        sync->slave_pulse_count = 0;
    }
    portEXIT_CRITICAL(&sync->spinlock);
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
    // Only set user_data if not already set by transport callback
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
    
    // For multiplied clocks, we output multiple pulses per internal 24 PPQN tick
    // For divided clocks, we output fewer pulses
    // Internal pulse_count always tracks at effective PPQN
    
    // Calculate internal pulse position (normalized to 24 PPQN equivalent)
    int effective_ppqn = sync->effective_ppqn;
    uint32_t internal_pulse = sync->pulse_count;
    
    // Map pulse to 24 PPQN equivalent for swing calculation
    uint32_t pulse_24ppqn_equiv;
    if (sync->clock_rate < 0) {
        // Divider: each output pulse = multiple internal pulses
        pulse_24ppqn_equiv = internal_pulse * (-sync->clock_rate);
    } else if (sync->clock_rate > 1) {
        // Multiplier: multiple output pulses per internal pulse
        pulse_24ppqn_equiv = internal_pulse / sync->clock_rate;
    } else {
        pulse_24ppqn_equiv = internal_pulse;
    }
    
    // Calculate swing delay for off-beat 16th notes (at 24 PPQN equivalent)
    // Swing applies to pulses 6, 18 (the "ands" of beats at 24ppqn)
    uint32_t pulse_in_beat_24 = pulse_24ppqn_equiv % ANALOG_SYNC_PPQN;
    bool is_swing_pulse = (pulse_in_beat_24 == 6) || (pulse_in_beat_24 == 18);
    
    if (is_swing_pulse && sync->swing_ms > 0.0f) {
        uint32_t swing_us = (uint32_t)(sync->swing_ms * 1000.0f);
        esp_rom_delay_us(swing_us > 1000 ? 1000 : swing_us);
    }
    
    // Generate pulse
    if (sync->clock_out_pin >= 0) {
        gpio_set_level(sync->clock_out_pin, 1);
        sync->pulse_state = true;
        esp_timer_start_once(sync->pulse_off_timer, PULSE_WIDTH_US);
    }
    
    // Update counters (using effective PPQN)
    sync->pulse_count++;
    sync->phase = (float)(sync->pulse_count % effective_ppqn) / (float)effective_ppqn;
    
    // Update beat position (normalized to standard timing)
    sync->beat_position = (double)sync->pulse_count / (double)effective_ppqn;
    
    // Update bar/quantum phase
    uint32_t pulses_per_quantum = (uint32_t)(sync->quantum * effective_ppqn);
    if (pulses_per_quantum > 0) {
        uint32_t pulse_in_bar = sync->pulse_count % pulses_per_quantum;
        sync->bar_phase = (double)pulse_in_bar / (double)pulses_per_quantum * sync->quantum;
    }
    
    // Fire beat callback on beat boundaries (every effective_ppqn pulses)
    if ((sync->pulse_count % effective_ppqn) == 0) {
        sync->last_beat_time_us = esp_timer_get_time();
        if (sync->transport_callback) {
            sync->transport_callback(sync, ANALOG_SYNC_EVENT_BEAT, 
                                     sync->callback_user_data);
        }
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
        portENTER_CRITICAL(&sync->spinlock);
        sync->clock_locked = false;
        sync->running = false;
        sync->pending.clock_lost = 1;
        portEXIT_CRITICAL(&sync->spinlock);
        
        ESP_LOGW(TAG, "DIN sync clock lost (timeout)");
    }
}

// ============================================================================
// ISR handlers (slave mode)
// ============================================================================

/**
 * @brief Clock input ISR - called on each rising edge of clock signal
 * 
 * Measures time between pulses to recover tempo, and tracks phase.
 * Heavy lifting (callbacks, LED updates) deferred to tick().
 */
static void IRAM_ATTR clock_input_isr(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    uint64_t now_us = esp_timer_get_time();
    
    // Measure period since last pulse
    if (sync->last_pulse_time_us > 0) {
        uint64_t delta_us = now_us - sync->last_pulse_time_us;
        
        // Sanity check: reject obviously invalid periods
        // Valid range: ~1ms (300 BPM * 24ppqn) to ~83ms (30 BPM * 24ppqn)
        if (delta_us > 800 && delta_us < 100000) {
            // Add to jitter filter
            uint64_t filtered_us = jitter_filter_add(&sync->jitter, delta_us);
            
            if (filtered_us > 0) {
                sync->current_period_us = filtered_us;
                
                // Calculate BPM using effective PPQN (respects clock rate setting)
                // This allows interpreting incoming clock at different rates
                float new_bpm = period_us_to_bpm_ppqn(filtered_us, sync->effective_ppqn);
                new_bpm = clamp_bpm(new_bpm);
                
                // Update PLL for drift correction
                pll_update(&sync->pll, now_us, filtered_us);
                
                // Use PLL-corrected BPM when locked
                if (sync->clock_locked) {
                    // Get smoothed BPM from PLL
                    float pll_bpm = pll_get_corrected_bpm(&sync->pll, new_bpm);
                    // Blend with jitter-filtered measurement
                    sync->slave_bpm = sync->slave_bpm * (1.0f - sync->pll.alpha) + 
                                      pll_bpm * sync->pll.alpha;
                } else {
                    sync->slave_bpm = new_bpm;
                }
                
                // Track drift statistics
                drift_tracker_add(&sync->drift, delta_us, 
                                  bpm_to_period_us(sync->slave_bpm));
                
                // Lock after minimum samples
                if (!sync->clock_locked && sync->jitter.sample_count >= MIN_LOCK_SAMPLES) {
                    sync->clock_locked = true;
                    if (sync->running) {
                        // Already running from RUN signal
                    } else if (sync->run_in_pin < 0) {
                        // No RUN input - auto-start on clock lock
                        sync->running = true;
                        portENTER_CRITICAL_ISR(&sync->spinlock);
                        sync->pending.transport_start = 1;
                        portEXIT_CRITICAL_ISR(&sync->spinlock);
                    }
                }
            }
        }
    }
    
    sync->last_pulse_time_us = now_us;
    sync->slave_pulse_count++;
    
    // Use effective PPQN based on clock rate setting
    // This allows interpreting incoming clock at different rates
    int effective_ppqn = sync->effective_ppqn;
    
    // Update phase
    uint32_t pulse_in_beat = sync->slave_pulse_count % effective_ppqn;
    sync->phase = (float)pulse_in_beat / (float)effective_ppqn;
    
    // Update beat position (fractional beats since start)
    sync->beat_position = (double)sync->slave_pulse_count / (double)effective_ppqn;
    
    // Update bar/quantum phase
    uint32_t pulses_per_quantum = (uint32_t)(sync->quantum * effective_ppqn);
    if (pulses_per_quantum > 0) {
        uint32_t pulse_in_bar = sync->slave_pulse_count % pulses_per_quantum;
        sync->bar_phase = (double)pulse_in_bar / (double)pulses_per_quantum * sync->quantum;
    }
    
    // Fire beat event on beat boundaries
    if (pulse_in_beat == 0 && sync->running) {
        sync->last_beat_time_us = now_us;
        portENTER_CRITICAL_ISR(&sync->spinlock);
        sync->pending.beat = 1;
        portEXIT_CRITICAL_ISR(&sync->spinlock);
    }
}

/**
 * @brief RUN input ISR - called on any edge of run/stop signal
 * 
 * Rising edge = start, falling edge = stop.
 */
static void IRAM_ATTR run_input_isr(void *arg) {
    analog_sync_t *sync = (analog_sync_t *)arg;
    
    int level = gpio_get_level(sync->run_in_pin);
    
    portENTER_CRITICAL_ISR(&sync->spinlock);
    if (level) {
        // RUN signal high - start transport
        sync->running = true;
        sync->slave_pulse_count = 0;
        sync->phase = 0.0f;
        sync->pending.transport_start = 1;
    } else {
        // RUN signal low - stop transport
        sync->running = false;
        sync->pending.transport_stop = 1;
    }
    portEXIT_CRITICAL_ISR(&sync->spinlock);
}

// ============================================================================
// Jitter filter implementation
// ============================================================================

static void jitter_filter_init(jitter_filter_t *filter) {
    memset(filter, 0, sizeof(jitter_filter_t));
}

/**
 * @brief Add sample to jitter filter and return filtered period
 * 
 * Uses median filtering to reject outliers from the clock signal.
 * Returns 0 until minimum samples collected.
 */
static uint64_t jitter_filter_add(jitter_filter_t *filter, uint64_t period_us) {
    // Store sample
    filter->samples[filter->write_index] = period_us;
    filter->write_index = (filter->write_index + 1) & JITTER_FILTER_MASK;
    
    if (filter->sample_count < JITTER_FILTER_SIZE) {
        filter->sample_count++;
    }
    
    // Need at least 3 samples for median
    if (filter->sample_count < 3) {
        return 0;
    }
    
    // Calculate median using insertion sort (efficient for small N)
    uint64_t sorted[JITTER_FILTER_SIZE];
    int n = filter->sample_count;
    
    // Copy samples
    for (int i = 0; i < n; i++) {
        sorted[i] = filter->samples[i];
    }
    
    // Insertion sort
    for (int i = 1; i < n; i++) {
        uint64_t key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    
    // Return median
    filter->filtered_period_us = sorted[n / 2];
    return filter->filtered_period_us;
}

// ============================================================================
// PLL-based drift correction
// ============================================================================

/**
 * @brief Get PLL alpha coefficient from bandwidth preset
 */
static float get_pll_alpha(analog_sync_pll_bandwidth_t bandwidth) {
    switch (bandwidth) {
        case ANALOG_SYNC_PLL_TIGHT:  return PLL_ALPHA_TIGHT;
        case ANALOG_SYNC_PLL_SMOOTH: return PLL_ALPHA_SMOOTH;
        case ANALOG_SYNC_PLL_NORMAL:
        default:                     return PLL_ALPHA_NORMAL;
    }
}

/**
 * @brief Initialize PLL state
 */
static void pll_init(pll_state_t *pll, float alpha) {
    memset(pll, 0, sizeof(pll_state_t));
    pll->alpha = alpha;
    pll->beta = alpha * alpha * 0.25f;  // Critically damped response
    pll->frequency = 0.0;
    pll->phase = 0.0;
    pll->initialized = false;
}

/**
 * @brief Update PLL with new measurement
 * 
 * Implements a type-2 PLL that tracks both phase and frequency.
 * The phase detector compares measured arrival time to predicted time.
 */
static void pll_update(pll_state_t *pll, int64_t measured_time_us, int64_t expected_period_us) {
    if (!pll->initialized) {
        // First sample - initialize
        pll->last_update_us = measured_time_us;
        pll->frequency = 1000000.0 / (double)expected_period_us;  // Hz
        pll->phase = 0.0;
        pll->initialized = true;
        return;
    }
    
    // Calculate expected arrival time based on current frequency
    double period_s = 1.0 / pll->frequency;
    int64_t expected_time_us = pll->last_update_us + (int64_t)(period_s * 1000000.0);
    
    // Phase error = (measured - expected) / period
    double error_us = (double)(measured_time_us - expected_time_us);
    double error_normalized = error_us / (period_s * 1000000.0);
    
    // Clamp error to avoid large jumps from glitches
    if (error_normalized > 0.5) error_normalized = 0.5;
    if (error_normalized < -0.5) error_normalized = -0.5;
    
    pll->phase_error = error_normalized;
    
    // Update frequency estimate (integral term)
    pll->freq_error += error_normalized * pll->beta;
    
    // Apply corrections
    double freq_correction = error_normalized * pll->alpha + pll->freq_error;
    pll->frequency *= (1.0 + freq_correction);
    
    // Clamp frequency to valid range (30-300 BPM at 24 PPQN)
    double min_freq = (30.0 * ANALOG_SYNC_PPQN) / 60.0;   // ~12 Hz
    double max_freq = (300.0 * ANALOG_SYNC_PPQN) / 60.0;  // ~120 Hz
    if (pll->frequency < min_freq) pll->frequency = min_freq;
    if (pll->frequency > max_freq) pll->frequency = max_freq;
    
    // Update phase accumulator
    double elapsed_s = (double)(measured_time_us - pll->last_update_us) / 1000000.0;
    pll->phase += elapsed_s * pll->frequency;
    pll->phase = fmod(pll->phase, 1.0);  // Keep in [0, 1)
    
    pll->last_update_us = measured_time_us;
}

/**
 * @brief Get PLL-corrected BPM
 */
static float pll_get_corrected_bpm(const pll_state_t *pll, float nominal_bpm) {
    if (!pll->initialized) {
        return nominal_bpm;
    }
    // Convert frequency (pulses/second) to BPM
    return (float)(pll->frequency * 60.0 / ANALOG_SYNC_PPQN);
}

// ============================================================================
// Drift tracking
// ============================================================================

/**
 * @brief Initialize drift tracker
 */
static void drift_tracker_init(drift_tracker_t *drift) {
    memset(drift, 0, sizeof(drift_tracker_t));
}

/**
 * @brief Add sample to drift tracker
 */
static void drift_tracker_add(drift_tracker_t *drift, int64_t period_us, int64_t expected_us) {
    // Reject obvious glitches (>50% deviation)
    if (expected_us > 0) {
        int64_t deviation = period_us - expected_us;
        if (deviation > expected_us / 2 || deviation < -expected_us / 2) {
            drift->glitch_count++;
            return;
        }
    }
    
    // Store sample
    uint32_t idx = drift->write_index;
    
    // Update running sum (subtract old value if buffer is full)
    if (drift->sample_count >= DRIFT_WINDOW_SIZE) {
        drift->sum_period_us -= drift->period_samples[idx];
    }
    
    drift->period_samples[idx] = period_us;
    drift->sum_period_us += period_us;
    drift->write_index = (idx + 1) & DRIFT_WINDOW_MASK;
    
    if (drift->sample_count < DRIFT_WINDOW_SIZE) {
        drift->sample_count++;
    }
    
    drift->expected_period_us = expected_us;
    
    // Calculate drift in PPB
    if (drift->sample_count >= 4 && expected_us > 0) {
        int64_t avg_period = drift->sum_period_us / drift->sample_count;
        int64_t drift_us = avg_period - expected_us;
        drift->drift_ppb = (float)PPB_PER_US_PER_PERIOD(drift_us, expected_us);
    }
}

// ============================================================================
// LED state management
// ============================================================================

static void update_led_state(analog_sync_t *sync, analog_sync_led_state_t state) {
    if (!sync || sync->led_state == state) return;
    
    sync->led_state = state;
    
    if (sync->led_callback) {
        sync->led_callback(sync, state, sync->callback_user_data);
    }
}

// ============================================================================
// Sync manager integration - tick function
// ============================================================================

void analog_sync_tick(analog_sync_t *sync) {
    if (!sync) return;
    
    // Process pending events from ISR
    pending_events_t events;
    portENTER_CRITICAL(&sync->spinlock);
    events = sync->pending;
    memset((void *)&sync->pending, 0, sizeof(pending_events_t));
    portEXIT_CRITICAL(&sync->spinlock);
    
    // Handle transport events
    if (events.transport_start && sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_START, 
                                 sync->callback_user_data);
    }
    
    if (events.transport_stop && sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_STOP, 
                                 sync->callback_user_data);
    }
    
    if (events.clock_lost) {
        update_led_state(sync, ANALOG_SYNC_LED_WAITING);
        if (sync->transport_callback) {
            sync->transport_callback(sync, ANALOG_SYNC_EVENT_CLOCK_LOST, 
                                     sync->callback_user_data);
        }
    }
    
    if (events.beat && sync->transport_callback) {
        sync->transport_callback(sync, ANALOG_SYNC_EVENT_BEAT, 
                                 sync->callback_user_data);
    }
    
    // Update LED state for slave mode
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
        
        // Reset timeout timer
        esp_timer_stop(sync->timeout_timer);
        if (sync->clock_locked) {
            esp_timer_start_once(sync->timeout_timer, CLOCK_TIMEOUT_MS * 1000);
        }
    }
}

// ============================================================================
// Debug/diagnostics
// ============================================================================

void analog_sync_get_diagnostics(const analog_sync_t *sync, 
                                  float *bpm_out,
                                  uint32_t *pulse_count_out,
                                  float *phase_out,
                                  bool *locked_out) {
    if (!sync) return;
    
    if (bpm_out) {
        *bpm_out = (sync->mode == ANALOG_SYNC_MODE_SLAVE) ? 
                   sync->slave_bpm : sync->master_bpm;
    }
    if (pulse_count_out) {
        *pulse_count_out = (sync->mode == ANALOG_SYNC_MODE_MASTER) ?
                           sync->pulse_count : sync->slave_pulse_count;
    }
    if (phase_out) {
        *phase_out = sync->phase;
    }
    if (locked_out) {
        *locked_out = sync->clock_locked;
    }
}

// ============================================================================
// Quantum and PLL configuration
// ============================================================================

void analog_sync_set_quantum(analog_sync_t *sync, float quantum) {
    if (!sync) return;
    
    // Clamp to reasonable range (1-16 beats)
    if (quantum < 1.0f) quantum = 1.0f;
    if (quantum > 16.0f) quantum = 16.0f;
    
    sync->quantum = quantum;
    ESP_LOGD(TAG, "Quantum set to %.1f beats", quantum);
}

float analog_sync_get_quantum(const analog_sync_t *sync) {
    return sync ? sync->quantum : DEFAULT_QUANTUM;
}

void analog_sync_set_pll_bandwidth(analog_sync_t *sync, 
                                    analog_sync_pll_bandwidth_t bandwidth) {
    if (!sync) return;
    
    sync->pll_bandwidth = bandwidth;
    sync->pll.alpha = get_pll_alpha(bandwidth);
    sync->pll.beta = sync->pll.alpha * sync->pll.alpha * 0.25f;
    
    ESP_LOGD(TAG, "PLL bandwidth set to %d (alpha=%.3f)", 
             bandwidth, sync->pll.alpha);
}

analog_sync_pll_bandwidth_t analog_sync_get_pll_bandwidth(const analog_sync_t *sync) {
    return sync ? sync->pll_bandwidth : ANALOG_SYNC_PLL_NORMAL;
}

void analog_sync_align_to_quantum(analog_sync_t *sync) {
    if (!sync) return;
    
    portENTER_CRITICAL(&sync->spinlock);
    
    // Calculate pulses per quantum using effective PPQN
    int effective_ppqn = sync->effective_ppqn;
    uint32_t pulses_per_quantum = (uint32_t)(sync->quantum * effective_ppqn);
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        // Round pulse count to nearest quantum boundary
        uint32_t current_in_quantum = sync->pulse_count % pulses_per_quantum;
        if (current_in_quantum > pulses_per_quantum / 2) {
            // Closer to next boundary - snap forward
            sync->pulse_count += (pulses_per_quantum - current_in_quantum);
        } else {
            // Closer to previous boundary - snap back
            sync->pulse_count -= current_in_quantum;
        }
    } else {
        // Slave mode - reset to quantum boundary
        uint32_t current_in_quantum = sync->slave_pulse_count % pulses_per_quantum;
        sync->slave_pulse_count -= current_in_quantum;
    }
    
    sync->bar_phase = 0.0;
    sync->beat_position = (double)((sync->mode == ANALOG_SYNC_MODE_MASTER) ? 
                          sync->pulse_count : sync->slave_pulse_count) / effective_ppqn;
    
    portEXIT_CRITICAL(&sync->spinlock);
    
    ESP_LOGI(TAG, "Phase aligned to quantum boundary");
}

void analog_sync_request_beat_at_time(analog_sync_t *sync, 
                                       double beat, 
                                       int64_t time_us) {
    if (!sync || sync->mode != ANALOG_SYNC_MODE_MASTER) return;
    
    // Calculate the required tempo adjustment to hit the beat at the target time
    int64_t now_us = esp_timer_get_time();
    double current_beat = (double)sync->pulse_count / ANALOG_SYNC_PPQN;
    double beats_to_go = beat - current_beat;
    
    if (beats_to_go <= 0 || time_us <= now_us) {
        ESP_LOGW(TAG, "Cannot request beat in the past");
        return;
    }
    
    double time_to_go_s = (double)(time_us - now_us) / 1000000.0;
    double required_bpm = (beats_to_go / time_to_go_s) * 60.0;
    
    // Clamp to valid BPM range
    required_bpm = clamp_bpm((float)required_bpm);
    
    ESP_LOGI(TAG, "Adjusting tempo to %.2f BPM to hit beat %.1f at target time",
             required_bpm, beat);
    
    analog_sync_set_bpm(sync, (float)required_bpm);
}

// ============================================================================
// Thread-safe state capture
// ============================================================================

void analog_sync_capture_state(const analog_sync_t *sync, 
                                analog_sync_state_t *state) {
    if (!sync || !state) return;
    
    // Take snapshot atomically
    portENTER_CRITICAL((portMUX_TYPE *)&sync->spinlock);
    
    int64_t now_us = esp_timer_get_time();
    
    state->time_us = now_us;
    state->is_running = sync->running;
    state->is_locked = sync->clock_locked;
    
    // Use effective PPQN for beat calculations
    int effective_ppqn = sync->effective_ppqn;
    
    if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
        state->bpm = sync->master_bpm;
        state->pulse_count = sync->pulse_count;
        state->phase = sync->phase;
        state->beat = (double)sync->pulse_count / effective_ppqn;
    } else {
        state->bpm = sync->slave_bpm;
        state->pulse_count = sync->slave_pulse_count;
        state->phase = sync->phase;
        state->beat = sync->beat_position;
    }
    
    state->bar_phase = sync->bar_phase;
    state->drift_ppb = sync->drift.drift_ppb;
    
    // Calculate predicted times using effective PPQN
    if (state->bpm > 0) {
        // Period per pulse at effective PPQN
        uint64_t period_us = (uint64_t)(60000000.0 / (state->bpm * effective_ppqn));
        
        // Adjust period for clock rate (master mode)
        if (sync->mode == ANALOG_SYNC_MODE_MASTER) {
            if (sync->clock_rate < 0) {
                period_us = period_us * (-sync->clock_rate);
            } else if (sync->clock_rate > 1) {
                period_us = period_us / sync->clock_rate;
            }
        }
        
        // Time since last pulse
        uint64_t since_last_pulse = 0;
        if (sync->last_pulse_time_us > 0 && now_us > (int64_t)sync->last_pulse_time_us) {
            since_last_pulse = now_us - sync->last_pulse_time_us;
        }
        
        // Next pulse time
        if (since_last_pulse < period_us) {
            state->next_pulse_time_us = now_us + (period_us - since_last_pulse);
        } else {
            state->next_pulse_time_us = now_us + period_us;
        }
        
        // Next beat time
        uint32_t pulses_to_beat = effective_ppqn - (state->pulse_count % effective_ppqn);
        if (pulses_to_beat == (uint32_t)effective_ppqn) pulses_to_beat = 0;
        state->next_beat_time_us = now_us + (pulses_to_beat * period_us) - since_last_pulse;
    } else {
        state->next_pulse_time_us = 0;
        state->next_beat_time_us = 0;
    }
    
    portEXIT_CRITICAL((portMUX_TYPE *)&sync->spinlock);
}

double analog_sync_beat_at_time(const analog_sync_t *sync, int64_t time_us) {
    if (!sync) return 0.0;
    
    analog_sync_state_t state;
    analog_sync_capture_state(sync, &state);
    
    if (state.bpm <= 0) return state.beat;
    
    // Calculate beat at target time
    double elapsed_s = (double)(time_us - state.time_us) / 1000000.0;
    double beats_elapsed = elapsed_s * state.bpm / 60.0;
    
    return state.beat + beats_elapsed;
}

double analog_sync_phase_at_time(const analog_sync_t *sync, int64_t time_us) {
    if (!sync) return 0.0;
    
    double beat = analog_sync_beat_at_time(sync, time_us);
    float quantum = sync->quantum;
    
    // Phase within quantum (0.0 to quantum)
    double phase = fmod(beat, (double)quantum);
    if (phase < 0) phase += quantum;
    
    return phase;
}

int64_t analog_sync_time_at_beat(const analog_sync_t *sync, double beat) {
    if (!sync) return 0;
    
    analog_sync_state_t state;
    analog_sync_capture_state(sync, &state);
    
    if (state.bpm <= 0) return state.time_us;
    
    double beats_to_go = beat - state.beat;
    double seconds_to_go = beats_to_go * 60.0 / state.bpm;
    
    return state.time_us + (int64_t)(seconds_to_go * 1000000.0);
}

int64_t analog_sync_next_beat_time(const analog_sync_t *sync) {
    if (!sync) return 0;
    
    analog_sync_state_t state;
    analog_sync_capture_state(sync, &state);
    
    return state.next_beat_time_us;
}

int64_t analog_sync_next_quantum_time(const analog_sync_t *sync) {
    if (!sync) return 0;
    
    analog_sync_state_t state;
    analog_sync_capture_state(sync, &state);
    
    if (state.bpm <= 0) return state.time_us;
    
    // Find next quantum boundary
    double current_phase = fmod(state.beat, (double)sync->quantum);
    double beats_to_quantum = sync->quantum - current_phase;
    if (beats_to_quantum <= 0.001) beats_to_quantum = sync->quantum;  // Already at boundary
    
    double seconds_to_quantum = beats_to_quantum * 60.0 / state.bpm;
    
    return state.time_us + (int64_t)(seconds_to_quantum * 1000000.0);
}

// ============================================================================
// Drift statistics
// ============================================================================

void analog_sync_get_drift_stats(const analog_sync_t *sync,
                                  analog_sync_drift_stats_t *stats) {
    if (!sync || !stats) return;
    
    memset(stats, 0, sizeof(analog_sync_drift_stats_t));
    
    portENTER_CRITICAL((portMUX_TYPE *)&sync->spinlock);
    
    stats->drift_ppb = sync->drift.drift_ppb;
    stats->drift_ppm = sync->drift.drift_ppb / 1000.0f;
    stats->glitch_count = sync->drift.glitch_count;
    stats->total_pulses = (sync->mode == ANALOG_SYNC_MODE_MASTER) ?
                          sync->pulse_count : sync->slave_pulse_count;
    stats->lock_time_us = sync->drift.lock_time_us;
    
    // Calculate average period
    if (sync->drift.sample_count > 0) {
        stats->avg_period_us = (float)sync->drift.sum_period_us / 
                               (float)sync->drift.sample_count;
    }
    
    // Estimate jitter (simplified - using deviation from expected)
    if (sync->drift.expected_period_us > 0 && sync->drift.sample_count > 0) {
        float expected = (float)sync->drift.expected_period_us;
        float deviation = stats->avg_period_us - expected;
        stats->jitter_us = fabsf(deviation);
    }
    
    portEXIT_CRITICAL((portMUX_TYPE *)&sync->spinlock);
}

void analog_sync_reset_drift_stats(analog_sync_t *sync) {
    if (!sync) return;
    
    portENTER_CRITICAL(&sync->spinlock);
    drift_tracker_init(&sync->drift);
    pll_init(&sync->pll, get_pll_alpha(sync->pll_bandwidth));
    sync->drift.lock_time_us = esp_timer_get_time();
    portEXIT_CRITICAL(&sync->spinlock);
    
    ESP_LOGI(TAG, "Drift statistics reset");
}
