/**
 * @file jog_wheel.c
 * @brief Touch-sensitive jog wheel controller implementation
 * 
 * Provides comprehensive jog wheel control for DJ applications with:
 * - Capacitive touch detection with hysteresis and calibration
 * - High-resolution rotary encoder velocity calculation with smoothing
 * - Multiple operating modes (Vinyl, CDJ, Nudge-only, Search)
 * - Configurable response curves (linear, exponential, logarithmic, S-curve)
 * - Integration with slip mode for non-destructive scratching
 */

#include "jog_wheel.h"
#include "board_config.h"
#include "slip_mode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/touch_pad.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "jog_wheel";

/* ============================================================================
 * Configuration Defaults
 * ============================================================================ */

// Default encoder pulses per revolution (typical optical encoder)
#define DEFAULT_PPR                 600

// Default velocity calculation interval
#define DEFAULT_VELOCITY_UPDATE_MS  10

// Default touch debounce
#define DEFAULT_TOUCH_DEBOUNCE_MS   20

// Touch sensor defaults
#define DEFAULT_TOUCH_THRESHOLD_ON  50
#define DEFAULT_TOUCH_THRESHOLD_OFF 40
#define DEFAULT_TOUCH_BASELINE      0
#define DEFAULT_TOUCH_SAMPLES       4

// Sensitivity defaults
#define DEFAULT_SCRATCH_SENS        1.0f
#define DEFAULT_NUDGE_SENS          1.0f
#define DEFAULT_SEARCH_SENS         1.0f
#define DEFAULT_DEAD_ZONE           0.02f
#define DEFAULT_BRAKE_STRENGTH      0.5f

// Velocity decay when no rotation
#define VELOCITY_DECAY_FACTOR       0.85f

// Maximum velocity for normalization
#define MAX_VELOCITY_DPS            1800.0f  // Degrees per second

// Scratch mode output scaling
#define SCRATCH_SPEED_SCALE         2.0f     // Max playback speed

// Nudge mode output scaling  
#define NUDGE_PERCENT_SCALE         8.0f     // Max tempo adjustment %

// Search mode output scaling
#define SEARCH_SECONDS_SCALE        5.0f     // Seconds per rotation

/* ============================================================================
 * Module State
 * ============================================================================ */

typedef struct {
    // Configuration
    jog_config_t config;
    
    // State
    jog_state_t state;
    
    // Callbacks
    jog_event_cb_t event_callback;
    jog_touch_cb_t touch_callback;
    void *callback_arg;
    
    // Encoder state
    int8_t encoder_last_a;
    int8_t encoder_last_b;
    int32_t encoder_position;
    int32_t encoder_last_position;
    
    // Velocity calculation
    float velocity_history[JOG_VELOCITY_HISTORY_SIZE];
    int velocity_history_idx;
    uint32_t last_velocity_time_us;
    uint32_t last_update_time_ms;
    
    // Touch sensor state
    uint16_t touch_raw;
    uint16_t touch_filtered;
    bool touch_state;
    bool touch_last_state;
    uint32_t touch_change_time;
    
    // Calibration state
    bool calibrating;
    uint32_t cal_sample_count;
    uint32_t cal_baseline_sum;
    uint32_t cal_touched_sum;
    bool cal_baseline_done;
    bool cal_touched_done;
    
    // Mode override
    bool search_override;
    jog_mode_t mode_before_search;
    
    // Slip mode state
    bool slip_triggered;
    bool was_scratching;
    
    // Mutex
    SemaphoreHandle_t mutex;
    
    // Initialization flag
    bool initialized;
} jog_wheel_state_t;

static jog_wheel_state_t s_jog = {0};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Get current time in microseconds
 */
static inline uint64_t get_time_us(void) {
    return esp_timer_get_time();
}

/**
 * @brief Get current time in milliseconds
 */
static inline uint32_t get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief Clamp float value to range
 */
static inline float clamp_f(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * @brief Apply dead zone to value
 */
static float apply_dead_zone(float value, float dead_zone) {
    if (fabsf(value) < dead_zone) {
        return 0.0f;
    }
    // Scale remaining range to 0-1
    float sign = (value > 0) ? 1.0f : -1.0f;
    float abs_val = fabsf(value);
    return sign * (abs_val - dead_zone) / (1.0f - dead_zone);
}

/**
 * @brief Apply response curve to normalized velocity
 */
static float apply_curve(float velocity, jog_curve_t curve, const jog_custom_curve_t *custom) {
    float abs_vel = fabsf(velocity);
    float sign = (velocity >= 0) ? 1.0f : -1.0f;
    float result;
    
    switch (curve) {
        case JOG_CURVE_LINEAR:
            result = abs_vel;
            break;
            
        case JOG_CURVE_EXPONENTIAL:
            // More sensitive at high speeds
            result = abs_vel * abs_vel;
            break;
            
        case JOG_CURVE_LOGARITHMIC:
            // More sensitive at low speeds
            if (abs_vel > 0.001f) {
                result = logf(1.0f + abs_vel * 9.0f) / logf(10.0f);
            } else {
                result = 0.0f;
            }
            break;
            
        case JOG_CURVE_S_CURVE:
            // Smooth S-curve for fine control at extremes
            result = 0.5f * (1.0f - cosf(abs_vel * M_PI));
            break;
            
        case JOG_CURVE_CUSTOM:
            if (custom) {
                // Linear interpolation between custom curve points
                result = abs_vel; // Default
                for (int i = 0; i < JOG_CURVE_POINTS - 1; i++) {
                    if (abs_vel >= custom->input[i] && abs_vel <= custom->input[i+1]) {
                        float t = (abs_vel - custom->input[i]) / 
                                  (custom->input[i+1] - custom->input[i]);
                        result = custom->output[i] + 
                                 t * (custom->output[i+1] - custom->output[i]);
                        break;
                    }
                }
            } else {
                result = abs_vel;
            }
            break;
            
        default:
            result = abs_vel;
            break;
    }
    
    return sign * clamp_f(result, 0.0f, 1.0f);
}

/**
 * @brief Calculate smoothed velocity from history
 */
static float calculate_smoothed_velocity(void) {
    float sum = 0.0f;
    for (int i = 0; i < JOG_VELOCITY_HISTORY_SIZE; i++) {
        sum += s_jog.velocity_history[i];
    }
    return sum / JOG_VELOCITY_HISTORY_SIZE;
}

/**
 * @brief Update velocity history with new sample
 */
static void update_velocity_history(float velocity) {
    s_jog.velocity_history[s_jog.velocity_history_idx] = velocity;
    s_jog.velocity_history_idx = (s_jog.velocity_history_idx + 1) % JOG_VELOCITY_HISTORY_SIZE;
}

/**
 * @brief Read encoder GPIOs and return delta
 */
static int8_t read_encoder_delta(void) {
    if (!PIN_IS_VALID(JOG_WHEEL_A_PIN) || !PIN_IS_VALID(JOG_WHEEL_B_PIN)) {
        return 0;
    }
    
    int8_t a = gpio_get_level(JOG_WHEEL_A_PIN);
    int8_t b = gpio_get_level(JOG_WHEEL_B_PIN);
    
    int8_t delta = 0;
    
    if (a != s_jog.encoder_last_a || b != s_jog.encoder_last_b) {
        // Quadrature decoding
        int8_t state = (s_jog.encoder_last_a << 1) | s_jog.encoder_last_b;
        int8_t new_state = (a << 1) | b;
        
        // State machine for direction detection
        // CW:  00->01->11->10->00
        // CCW: 00->10->11->01->00
        static const int8_t enc_table[16] = {
            0,  1, -1,  0,
           -1,  0,  0,  1,
            1,  0,  0, -1,
            0, -1,  1,  0
        };
        
        delta = enc_table[(state << 2) | new_state];
        
        if (s_jog.config.encoder_inverted) {
            delta = -delta;
        }
        
        s_jog.encoder_last_a = a;
        s_jog.encoder_last_b = b;
    }
    
    return delta;
}

/**
 * @brief Read touch sensor
 */
static uint16_t read_touch_sensor(void) {
    if (!PIN_IS_VALID(JOG_WHEEL_TOUCH_PIN)) {
        return 0;
    }
    
    // Simple GPIO read (active low with pull-up)
    return gpio_get_level(JOG_WHEEL_TOUCH_PIN) ? 0 : 100;
}

/**
 * @brief Process touch detection with hysteresis
 */
static bool process_touch(uint16_t raw_value) {
    const jog_touch_cal_t *cal = &s_jog.config.touch_cal;
    
    // Simple exponential moving average filter
    s_jog.touch_filtered = (uint16_t)((s_jog.touch_filtered * 3 + raw_value) / 4);
    
    // Apply hysteresis
    bool touched;
    if (s_jog.touch_state) {
        // Currently touched - need to drop below threshold_off
        touched = (s_jog.touch_filtered >= cal->threshold_off);
    } else {
        // Currently not touched - need to rise above threshold_on
        touched = (s_jog.touch_filtered >= cal->threshold_on);
    }
    
    // Invert if configured
    if (cal->inverted) {
        touched = !touched;
    }
    
    return touched;
}

/**
 * @brief Determine current action based on mode and state
 */
static jog_action_t determine_action(void) {
    jog_mode_t mode = s_jog.search_override ? JOG_MODE_SEARCH : s_jog.config.default_mode;
    
    if (mode == JOG_MODE_DISABLED) {
        return JOG_ACTION_NONE;
    }
    
    bool touched = s_jog.state.touched;
    bool rotating = (fabsf(s_jog.state.velocity) > 0.01f);
    
    switch (mode) {
        case JOG_MODE_VINYL:
            if (touched && rotating) {
                return JOG_ACTION_SCRATCH;
            } else if (touched && !rotating && s_jog.config.auto_brake_on_touch) {
                return JOG_ACTION_BRAKE;
            } else if (!touched && rotating) {
                return JOG_ACTION_NUDGE;
            }
            break;
            
        case JOG_MODE_CDJ:
            if (touched && rotating) {
                return JOG_ACTION_SCRATCH;
            } else if (touched && !rotating && s_jog.config.auto_brake_on_touch) {
                return JOG_ACTION_BRAKE;
            } else if (!touched && rotating) {
                return JOG_ACTION_SEARCH;
            }
            break;
            
        case JOG_MODE_NUDGE_ONLY:
            if (rotating) {
                return JOG_ACTION_NUDGE;
            }
            break;
            
        case JOG_MODE_SEARCH:
            if (rotating) {
                return JOG_ACTION_SEARCH;
            }
            break;
            
        default:
            break;
    }
    
    return JOG_ACTION_NONE;
}

/**
 * @brief Calculate output values based on action
 */
static void calculate_outputs(void) {
    const jog_sensitivity_t *sens = &s_jog.config.sensitivity;
    float velocity = s_jog.state.velocity;
    
    // Reset all outputs
    s_jog.state.scratch_value = 0.0f;
    s_jog.state.nudge_value = 0.0f;
    s_jog.state.search_value = 0.0f;
    s_jog.state.brake_value = 0.0f;
    
    switch (s_jog.state.action) {
        case JOG_ACTION_SCRATCH:
            // Scratch: velocity maps to playback speed
            // 0 velocity = stopped, 1.0 velocity = normal speed
            s_jog.state.scratch_value = velocity * sens->scratch_sensitivity * SCRATCH_SPEED_SCALE;
            s_jog.state.scratch_value = clamp_f(s_jog.state.scratch_value, -SCRATCH_SPEED_SCALE, SCRATCH_SPEED_SCALE);
            break;
            
        case JOG_ACTION_NUDGE:
            // Nudge: velocity maps to tempo adjustment
            s_jog.state.nudge_value = velocity * sens->nudge_sensitivity;
            s_jog.state.nudge_value = clamp_f(s_jog.state.nudge_value, -1.0f, 1.0f);
            break;
            
        case JOG_ACTION_SEARCH:
            // Search: velocity maps to seek rate
            s_jog.state.search_value = velocity * sens->search_sensitivity * SEARCH_SECONDS_SCALE;
            break;
            
        case JOG_ACTION_BRAKE:
            // Brake: intensity based on touch duration
            {
                float brake_time = (float)s_jog.state.touch_duration / 1000.0f;
                s_jog.state.brake_value = clamp_f(brake_time * sens->brake_strength, 0.0f, 1.0f);
            }
            break;
            
        default:
            break;
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

jog_config_t jog_wheel_get_default_config(void) {
    jog_config_t config = {
        .default_mode = JOG_MODE_VINYL,
        .response_curve = JOG_CURVE_LINEAR,
        .sensitivity = {
            .scratch_sensitivity = DEFAULT_SCRATCH_SENS,
            .nudge_sensitivity = DEFAULT_NUDGE_SENS,
            .search_sensitivity = DEFAULT_SEARCH_SENS,
            .dead_zone = DEFAULT_DEAD_ZONE,
            .brake_strength = DEFAULT_BRAKE_STRENGTH,
        },
        .touch_cal = {
            .threshold_on = DEFAULT_TOUCH_THRESHOLD_ON,
            .threshold_off = DEFAULT_TOUCH_THRESHOLD_OFF,
            .baseline = DEFAULT_TOUCH_BASELINE,
            .sample_count = DEFAULT_TOUCH_SAMPLES,
            .inverted = false,
        },
        .custom_curve = {
            .input = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f},
            .output = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f},
        },
        .pulses_per_revolution = DEFAULT_PPR,
        .encoder_inverted = false,
        .velocity_update_ms = DEFAULT_VELOCITY_UPDATE_MS,
        .touch_debounce_ms = DEFAULT_TOUCH_DEBOUNCE_MS,
        .slip_mode_integration = true,
        .auto_brake_on_touch = true,
    };
    
    return config;
}

bool jog_wheel_init(const jog_config_t *config) {
    if (s_jog.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing jog wheel controller");
    
    // Apply configuration
    if (config) {
        s_jog.config = *config;
    } else {
        s_jog.config = jog_wheel_get_default_config();
    }
    
    // Create mutex
    s_jog.mutex = xSemaphoreCreateMutex();
    if (!s_jog.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Configure encoder GPIOs if valid
    if (PIN_IS_VALID(JOG_WHEEL_A_PIN) && PIN_IS_VALID(JOG_WHEEL_B_PIN)) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << JOG_WHEEL_A_PIN) | (1ULL << JOG_WHEEL_B_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        
        s_jog.encoder_last_a = gpio_get_level(JOG_WHEEL_A_PIN);
        s_jog.encoder_last_b = gpio_get_level(JOG_WHEEL_B_PIN);
        
        ESP_LOGI(TAG, "Encoder configured (A=%d, B=%d)", JOG_WHEEL_A_PIN, JOG_WHEEL_B_PIN);
    } else {
        ESP_LOGW(TAG, "Encoder pins not configured");
    }
    
    // Configure touch GPIO if valid
    if (PIN_IS_VALID(JOG_WHEEL_TOUCH_PIN)) {
        gpio_config_t touch_conf = {
            .pin_bit_mask = (1ULL << JOG_WHEEL_TOUCH_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&touch_conf);
        
        ESP_LOGI(TAG, "Touch sensor configured (pin=%d)", JOG_WHEEL_TOUCH_PIN);
    } else {
        ESP_LOGW(TAG, "Touch sensor pin not configured");
    }
    
    // Initialize state
    memset(&s_jog.state, 0, sizeof(jog_state_t));
    s_jog.state.mode = s_jog.config.default_mode;
    
    for (int i = 0; i < JOG_VELOCITY_HISTORY_SIZE; i++) {
        s_jog.velocity_history[i] = 0.0f;
    }
    
    s_jog.last_velocity_time_us = get_time_us();
    s_jog.last_update_time_ms = get_time_ms();
    
    s_jog.initialized = true;
    
    ESP_LOGI(TAG, "Jog wheel initialized (mode=%d, PPR=%d)", 
             s_jog.config.default_mode, s_jog.config.pulses_per_revolution);
    
    return true;
}

void jog_wheel_deinit(void) {
    if (!s_jog.initialized) {
        return;
    }
    
    if (s_jog.mutex) {
        vSemaphoreDelete(s_jog.mutex);
    }
    
    memset(&s_jog, 0, sizeof(s_jog));
    
    ESP_LOGI(TAG, "Jog wheel deinitialized");
}

void jog_wheel_set_callbacks(jog_event_cb_t event_cb, jog_touch_cb_t touch_cb, void *arg) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    s_jog.event_callback = event_cb;
    s_jog.touch_callback = touch_cb;
    s_jog.callback_arg = arg;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_update(void) {
    if (!s_jog.initialized) {
        return;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    uint64_t now_us = get_time_us();
    uint32_t now_ms = get_time_ms();
    
    // Read encoder
    int8_t delta = read_encoder_delta();
    s_jog.encoder_position += delta;
    s_jog.state.delta = s_jog.encoder_position - s_jog.encoder_last_position;
    s_jog.state.position = s_jog.encoder_position;
    
    // Calculate velocity
    uint32_t dt_us = now_us - s_jog.last_velocity_time_us;
    if (dt_us >= s_jog.config.velocity_update_ms * 1000) {
        if (s_jog.state.delta != 0 && dt_us > 0) {
            // Calculate degrees per second
            float pulses = (float)s_jog.state.delta;
            float revolutions = pulses / (float)s_jog.config.pulses_per_revolution;
            float degrees = revolutions * 360.0f;
            float seconds = (float)dt_us / 1000000.0f;
            float dps = degrees / seconds;
            
            // Normalize to -1.0 to 1.0
            float velocity_raw = clamp_f(dps / MAX_VELOCITY_DPS, -1.0f, 1.0f);
            
            // Apply dead zone
            velocity_raw = apply_dead_zone(velocity_raw, s_jog.config.sensitivity.dead_zone);
            
            // Store raw velocity
            s_jog.state.velocity_raw = velocity_raw;
            
            // Apply response curve
            float velocity_curved = apply_curve(velocity_raw, 
                                                s_jog.config.response_curve,
                                                &s_jog.config.custom_curve);
            
            // Update history and smooth
            update_velocity_history(velocity_curved);
            s_jog.state.velocity = calculate_smoothed_velocity();
            
            // Update direction
            s_jog.state.direction = (s_jog.state.velocity > 0) ? 1.0f : 
                                    (s_jog.state.velocity < 0) ? -1.0f : 0.0f;
            
            // Track peak velocity
            float abs_vel = fabsf(s_jog.state.velocity);
            if (abs_vel > s_jog.state.peak_velocity) {
                s_jog.state.peak_velocity = abs_vel;
            }
        } else {
            // No movement - decay velocity
            s_jog.state.velocity *= VELOCITY_DECAY_FACTOR;
            if (fabsf(s_jog.state.velocity) < 0.001f) {
                s_jog.state.velocity = 0.0f;
            }
            update_velocity_history(s_jog.state.velocity);
        }
        
        s_jog.encoder_last_position = s_jog.encoder_position;
        s_jog.last_velocity_time_us = now_us;
    }
    
    // Read touch sensor
    s_jog.touch_raw = read_touch_sensor();
    bool touch_detected = process_touch(s_jog.touch_raw);
    
    // Apply touch debounce
    if (touch_detected != s_jog.touch_last_state) {
        s_jog.touch_change_time = now_ms;
    }
    
    if ((now_ms - s_jog.touch_change_time) >= s_jog.config.touch_debounce_ms) {
        if (touch_detected != s_jog.touch_state) {
            s_jog.touch_state = touch_detected;
            s_jog.state.touched = touch_detected;
            
            if (touch_detected) {
                s_jog.state.touch_start_time = now_ms;
                s_jog.state.touch_duration = 0;
            }
            
            // Fire touch callback
            if (s_jog.touch_callback) {
                s_jog.touch_callback(touch_detected, s_jog.callback_arg);
            }
            
            ESP_LOGD(TAG, "Touch %s", touch_detected ? "detected" : "released");
        }
    }
    s_jog.touch_last_state = touch_detected;
    
    // Update touch duration
    if (s_jog.state.touched) {
        s_jog.state.touch_duration = now_ms - s_jog.state.touch_start_time;
    }
    
    // Determine action and update mode
    s_jog.state.mode = s_jog.search_override ? JOG_MODE_SEARCH : s_jog.config.default_mode;
    jog_action_t new_action = determine_action();
    
    // Handle action transitions
    if (new_action != s_jog.state.action) {
        jog_action_t old_action = s_jog.state.action;
        s_jog.state.action = new_action;
        
        // Handle slip mode integration
        if (s_jog.config.slip_mode_integration) {
            if (new_action == JOG_ACTION_SCRATCH && old_action != JOG_ACTION_SCRATCH) {
                // Starting scratch - trigger slip mode
                s_jog.slip_triggered = true;
                s_jog.was_scratching = true;
                ESP_LOGD(TAG, "Scratch started - slip triggered");
            } else if (old_action == JOG_ACTION_SCRATCH && new_action != JOG_ACTION_SCRATCH) {
                // Ending scratch - end slip mode
                s_jog.slip_triggered = false;
                ESP_LOGD(TAG, "Scratch ended - slip released");
            }
        }
        
        // Track scratch sessions
        if (new_action == JOG_ACTION_SCRATCH) {
            s_jog.state.scratch_count++;
        }
        
        ESP_LOGD(TAG, "Action changed: %d -> %d", old_action, new_action);
    }
    
    // Calculate outputs
    calculate_outputs();
    
    // Fire event callback
    if (s_jog.event_callback && s_jog.state.action != JOG_ACTION_NONE) {
        float value = 0.0f;
        switch (s_jog.state.action) {
            case JOG_ACTION_SCRATCH: value = s_jog.state.scratch_value; break;
            case JOG_ACTION_NUDGE:   value = s_jog.state.nudge_value; break;
            case JOG_ACTION_SEARCH:  value = s_jog.state.search_value; break;
            case JOG_ACTION_BRAKE:   value = s_jog.state.brake_value; break;
            default: break;
        }
        s_jog.event_callback(s_jog.state.action, value, s_jog.callback_arg);
    }
    
    s_jog.state.update_count++;
    s_jog.last_update_time_ms = now_ms;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_process_encoder(int8_t delta) {
    if (!s_jog.initialized) return;
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    if (s_jog.config.encoder_inverted) {
        delta = -delta;
    }
    s_jog.encoder_position += delta;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_process_touch(uint16_t raw_value) {
    if (!s_jog.initialized) return;
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.touch_raw = raw_value;
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

/* ============================================================================
 * Mode Control
 * ============================================================================ */

void jog_wheel_set_mode(jog_mode_t mode) {
    if (mode >= JOG_MODE_COUNT) {
        ESP_LOGW(TAG, "Invalid mode: %d", mode);
        return;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.default_mode = mode;
    s_jog.state.mode = mode;
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    const char *mode_names[] = {"VINYL", "CDJ", "NUDGE", "SEARCH", "DISABLED"};
    ESP_LOGI(TAG, "Mode set to %s", mode_names[mode]);
}

jog_mode_t jog_wheel_get_mode(void) {
    return s_jog.state.mode;
}

void jog_wheel_cycle_mode(void) {
    jog_mode_t next = (s_jog.config.default_mode + 1) % (JOG_MODE_DISABLED);
    jog_wheel_set_mode(next);
}

void jog_wheel_search_mode(bool enable) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    if (enable && !s_jog.search_override) {
        s_jog.mode_before_search = s_jog.config.default_mode;
        s_jog.search_override = true;
        ESP_LOGD(TAG, "Search mode override enabled");
    } else if (!enable && s_jog.search_override) {
        s_jog.search_override = false;
        ESP_LOGD(TAG, "Search mode override disabled");
    }
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

/* ============================================================================
 * Sensitivity & Response
 * ============================================================================ */

void jog_wheel_set_scratch_sensitivity(float sensitivity) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.sensitivity.scratch_sensitivity = clamp_f(sensitivity, 0.1f, 10.0f);
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_set_nudge_sensitivity(float sensitivity) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.sensitivity.nudge_sensitivity = clamp_f(sensitivity, 0.1f, 10.0f);
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_set_search_sensitivity(float sensitivity) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.sensitivity.search_sensitivity = clamp_f(sensitivity, 0.1f, 10.0f);
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_set_curve(jog_curve_t curve) {
    if (curve >= JOG_CURVE_COUNT) {
        return;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.response_curve = curve;
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    const char *curve_names[] = {"LINEAR", "EXPONENTIAL", "LOGARITHMIC", "S_CURVE", "CUSTOM"};
    ESP_LOGI(TAG, "Response curve set to %s", curve_names[curve]);
}

void jog_wheel_set_custom_curve(const jog_custom_curve_t *curve) {
    if (!curve) return;
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.custom_curve = *curve;
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_set_dead_zone(float dead_zone) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.sensitivity.dead_zone = clamp_f(dead_zone, 0.0f, 0.5f);
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

void jog_wheel_set_brake_strength(float strength) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.sensitivity.brake_strength = clamp_f(strength, 0.0f, 1.0f);
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

/* ============================================================================
 * State Access
 * ============================================================================ */

const jog_state_t* jog_wheel_get_state(void) {
    return &s_jog.state;
}

bool jog_wheel_is_touched(void) {
    return s_jog.state.touched;
}

jog_action_t jog_wheel_get_action(void) {
    return s_jog.state.action;
}

float jog_wheel_get_velocity(void) {
    return s_jog.state.velocity;
}

float jog_wheel_get_scratch(void) {
    return s_jog.state.scratch_value;
}

float jog_wheel_get_nudge(void) {
    return s_jog.state.nudge_value;
}

float jog_wheel_get_search(void) {
    return s_jog.state.search_value;
}

float jog_wheel_get_brake(void) {
    return s_jog.state.brake_value;
}

/* ============================================================================
 * Touch Calibration
 * ============================================================================ */

bool jog_wheel_calibration_start(void) {
    if (!s_jog.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    s_jog.calibrating = true;
    s_jog.cal_sample_count = 0;
    s_jog.cal_baseline_sum = 0;
    s_jog.cal_touched_sum = 0;
    s_jog.cal_baseline_done = false;
    s_jog.cal_touched_done = false;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    ESP_LOGI(TAG, "Touch calibration started. Release jog wheel and call calibration_baseline().");
    return true;
}

void jog_wheel_calibration_baseline(void) {
    if (!s_jog.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    // Collect multiple samples
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += read_touch_sensor();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    s_jog.cal_baseline_sum = sum / 16;
    s_jog.cal_baseline_done = true;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    ESP_LOGI(TAG, "Baseline captured: %lu. Now touch the jog wheel and call calibration_touched().",
             (unsigned long)s_jog.cal_baseline_sum);
}

void jog_wheel_calibration_touched(void) {
    if (!s_jog.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    // Collect multiple samples
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += read_touch_sensor();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    s_jog.cal_touched_sum = sum / 16;
    s_jog.cal_touched_done = true;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    ESP_LOGI(TAG, "Touched value captured: %lu. Call calibration_finish() to apply.",
             (unsigned long)s_jog.cal_touched_sum);
}

bool jog_wheel_calibration_finish(void) {
    if (!s_jog.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return false;
    }
    
    if (!s_jog.cal_baseline_done || !s_jog.cal_touched_done) {
        ESP_LOGE(TAG, "Calibration incomplete (baseline=%d, touched=%d)",
                 s_jog.cal_baseline_done, s_jog.cal_touched_done);
        return false;
    }
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    // Determine if touch increases or decreases the reading
    bool inverted = (s_jog.cal_touched_sum < s_jog.cal_baseline_sum);
    
    uint16_t low_val, high_val;
    if (inverted) {
        low_val = s_jog.cal_touched_sum;
        high_val = s_jog.cal_baseline_sum;
    } else {
        low_val = s_jog.cal_baseline_sum;
        high_val = s_jog.cal_touched_sum;
    }
    
    // Calculate thresholds with hysteresis (~20% of range)
    uint16_t range = high_val - low_val;
    uint16_t hysteresis = range / 5;
    
    s_jog.config.touch_cal.baseline = s_jog.cal_baseline_sum;
    s_jog.config.touch_cal.threshold_on = low_val + (range * 6 / 10);
    s_jog.config.touch_cal.threshold_off = low_val + (range * 4 / 10);
    s_jog.config.touch_cal.inverted = inverted;
    
    s_jog.calibrating = false;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    ESP_LOGI(TAG, "Touch calibration applied: baseline=%u, on=%u, off=%u, inverted=%d",
             s_jog.config.touch_cal.baseline,
             s_jog.config.touch_cal.threshold_on,
             s_jog.config.touch_cal.threshold_off,
             s_jog.config.touch_cal.inverted);
    
    return true;
}

void jog_wheel_get_touch_calibration(jog_touch_cal_t *cal) {
    if (cal) {
        *cal = s_jog.config.touch_cal;
    }
}

bool jog_wheel_set_touch_calibration(const jog_touch_cal_t *cal) {
    if (!cal) return false;
    
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.touch_cal = *cal;
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    return true;
}

/* ============================================================================
 * Integration Helpers
 * ============================================================================ */

void jog_wheel_set_slip_integration(bool enable) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    s_jog.config.slip_mode_integration = enable;
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}

bool jog_wheel_slip_trigger(void) {
    bool triggered = s_jog.slip_triggered;
    return triggered;
}

float jog_wheel_get_playback_speed(void) {
    if (s_jog.state.action == JOG_ACTION_SCRATCH) {
        // During scratch, return the scratch speed directly
        return s_jog.state.scratch_value;
    }
    // Not scratching - return normal speed
    return 1.0f;
}

float jog_wheel_get_tempo_adjust(void) {
    if (s_jog.state.action == JOG_ACTION_NUDGE) {
        // Return tempo adjustment as percentage
        return s_jog.state.nudge_value * NUDGE_PERCENT_SCALE;
    }
    return 0.0f;
}

void jog_wheel_reset(void) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    s_jog.encoder_position = 0;
    s_jog.encoder_last_position = 0;
    s_jog.state.position = 0;
    s_jog.state.delta = 0;
    s_jog.state.velocity = 0.0f;
    s_jog.state.velocity_raw = 0.0f;
    s_jog.state.action = JOG_ACTION_NONE;
    
    for (int i = 0; i < JOG_VELOCITY_HISTORY_SIZE; i++) {
        s_jog.velocity_history[i] = 0.0f;
    }
    
    s_jog.slip_triggered = false;
    s_jog.was_scratching = false;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
    
    ESP_LOGI(TAG, "Jog wheel state reset");
}

/* ============================================================================
 * Debug & Statistics
 * ============================================================================ */

uint16_t jog_wheel_get_raw_touch(void) {
    return s_jog.touch_raw;
}

int32_t jog_wheel_get_raw_position(void) {
    return s_jog.encoder_position;
}

float jog_wheel_get_peak_velocity(void) {
    return s_jog.state.peak_velocity;
}

void jog_wheel_reset_stats(void) {
    if (s_jog.mutex) xSemaphoreTake(s_jog.mutex, portMAX_DELAY);
    
    s_jog.state.update_count = 0;
    s_jog.state.scratch_count = 0;
    s_jog.state.peak_velocity = 0.0f;
    
    if (s_jog.mutex) xSemaphoreGive(s_jog.mutex);
}
