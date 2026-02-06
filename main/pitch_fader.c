/**
 * @file pitch_fader.c
 * @brief Pitch fader (analog slider) implementation for DJ controller
 * 
 * Provides ADC-based pitch fader with:
 * - Calibrated ADC reading
 * - Center detent detection (dead zone)
 * - Range selection (±4%, ±8%, ±16%, ±50%)
 * - Exponential moving average filtering to reduce jitter
 * - Pitch bend functionality
 */

#include "pitch_fader.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <stdatomic.h>

static const char *TAG = "pitch_fader";

/* ============================================================================
 * Default Configuration
 * ============================================================================ */

// Default ADC channel (ADC1_CHANNEL_0 = GPIO1 on ESP32-S3)
// Override via pitch_fader_config_t or PITCH_FADER_ADC_CHANNEL in board_config.h
#ifndef PITCH_FADER_ADC_CHANNEL
#define PITCH_FADER_ADC_CHANNEL     ADC_CHANNEL_0
#endif

// Default filter coefficient (0.0-1.0, higher = more responsive, less smooth)
#define DEFAULT_FILTER_ALPHA        0.15f

// Default dead zone width (raw ADC units, ~2% of range)
#define DEFAULT_DEAD_ZONE           80

// ADC configuration
#define ADC_ATTEN                   ADC_ATTEN_DB_12     // Full range 0-3.3V
#define ADC_BITWIDTH                ADC_BITWIDTH_12     // 12-bit resolution (0-4095)
#define ADC_RAW_MAX                 4095

/* ============================================================================
 * Pitch Range Values
 * ============================================================================ */

static const float PITCH_RANGE_VALUES[PITCH_RANGE_COUNT] = {
    [PITCH_RANGE_4]  = 4.0f,
    [PITCH_RANGE_8]  = 8.0f,
    [PITCH_RANGE_16] = 16.0f,
    [PITCH_RANGE_50] = 50.0f,
};

/* ============================================================================
 * Module State
 * ============================================================================ */

typedef struct {
    // Configuration
    pitch_fader_config_t config;
    
    // ADC handles
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    bool has_calibration_scheme;
    
    // Current state
    uint16_t raw_value;             // Latest raw ADC reading
    float filtered_value;           // EMA-filtered raw value
    float position;                 // Normalized position (-1.0 to +1.0)
    float pitch;                    // Current pitch percentage
    bool centered;                  // True if in dead zone
    
    // Range
    pitch_range_t current_range;
    
    // Bend mode
    bool bend_mode;
    float bend_base_position;       // Position when bend mode was activated
    
    // Calibration mode
    bool calibrating;
    uint16_t cal_min_capture;
    uint16_t cal_max_capture;
    uint16_t cal_center_capture;
    bool cal_min_set;
    bool cal_max_set;
    bool cal_center_set;
    
    // Initialization flag
    bool initialized;
} pitch_fader_state_t;

static pitch_fader_state_t s_state = {0};

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

/**
 * @brief Initialize ADC for pitch fader
 */
static bool init_adc(int channel) {
    esp_err_t ret;
    
    // Configure ADC unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    ret = adc_oneshot_new_unit(&unit_cfg, &s_state.adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    
    ret = adc_oneshot_config_channel(s_state.adc_handle, channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_state.adc_handle);
        return false;
    }
    
    // Try to create calibration handle (optional, improves accuracy)
    s_state.has_calibration_scheme = false;
    
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_state.cali_handle);
    if (ret == ESP_OK) {
        s_state.has_calibration_scheme = true;
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!s_state.has_calibration_scheme) {
        adc_cali_line_fitting_config_t cali_cfg = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_state.cali_handle);
        if (ret == ESP_OK) {
            s_state.has_calibration_scheme = true;
            ESP_LOGI(TAG, "ADC calibration: line fitting");
        }
    }
#endif

    if (!s_state.has_calibration_scheme) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw values");
    }
    
    return true;
}

/**
 * @brief Read raw ADC value
 */
static uint16_t read_adc_raw(void) {
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_state.adc_handle, 
                                      s_state.config.adc_channel, 
                                      &raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return s_state.raw_value; // Return last known value
    }
    
    return (uint16_t)raw;
}

/**
 * @brief Apply EMA filter to raw value
 */
static float apply_filter(float new_value, float old_value, float alpha) {
    return alpha * new_value + (1.0f - alpha) * old_value;
}

/**
 * @brief Convert filtered raw value to normalized position (-1.0 to +1.0)
 */
static float raw_to_position(float raw, const pitch_fader_cal_t *cal) {
    float center = (float)cal->raw_center;
    float min = (float)cal->raw_min;
    float max = (float)cal->raw_max;
    float dead_zone = (float)cal->dead_zone;
    
    // Check dead zone first
    if (fabsf(raw - center) <= dead_zone) {
        return 0.0f;
    }
    
    float position;
    
    if (raw < center - dead_zone) {
        // Below center (negative range)
        float range = center - dead_zone - min;
        if (range <= 0) range = 1.0f; // Prevent division by zero
        position = -1.0f * (center - dead_zone - raw) / range;
    } else {
        // Above center (positive range)
        float range = max - (center + dead_zone);
        if (range <= 0) range = 1.0f;
        position = (raw - center - dead_zone) / range;
    }
    
    // Clamp to -1.0 to +1.0
    if (position < -1.0f) position = -1.0f;
    if (position > 1.0f) position = 1.0f;
    
    // Apply inversion if configured
    if (cal->inverted) {
        position = -position;
    }
    
    return position;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

pitch_fader_config_t pitch_fader_get_default_config(void) {
    pitch_fader_config_t config = {
        .adc_channel = PITCH_FADER_ADC_CHANNEL,
        .default_range = PITCH_RANGE_8,
        .filter_alpha = DEFAULT_FILTER_ALPHA,
        .calibration = {
            .raw_min = 0,
            .raw_max = ADC_RAW_MAX,
            .raw_center = ADC_RAW_MAX / 2,
            .dead_zone = DEFAULT_DEAD_ZONE,
            .inverted = false,
        },
    };
    return config;
}

bool pitch_fader_init(const pitch_fader_config_t *config) {
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    // Apply configuration
    if (config) {
        s_state.config = *config;
    } else {
        s_state.config = pitch_fader_get_default_config();
    }
    
    ESP_LOGI(TAG, "Initializing pitch fader (ADC channel %d)", s_state.config.adc_channel);
    
    // Initialize ADC
    if (!init_adc(s_state.config.adc_channel)) {
        return false;
    }
    
    // Initialize state
    s_state.current_range = s_state.config.default_range;
    s_state.raw_value = s_state.config.calibration.raw_center;
    s_state.filtered_value = (float)s_state.raw_value;
    s_state.position = 0.0f;
    s_state.pitch = 0.0f;
    s_state.centered = true;
    s_state.bend_mode = false;
    s_state.bend_base_position = 0.0f;
    s_state.calibrating = false;
    
    s_state.initialized = true;
    
    ESP_LOGI(TAG, "Pitch fader initialized (range: ±%.0f%%)", 
             PITCH_RANGE_VALUES[s_state.current_range]);
    
    return true;
}

void pitch_fader_deinit(void) {
    if (!s_state.initialized) {
        return;
    }
    
    // Delete calibration handle
    if (s_state.has_calibration_scheme) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_state.cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_state.cali_handle);
#endif
    }
    
    // Delete ADC unit
    if (s_state.adc_handle) {
        adc_oneshot_del_unit(s_state.adc_handle);
    }
    
    memset(&s_state, 0, sizeof(s_state));
    
    ESP_LOGI(TAG, "Pitch fader deinitialized");
}

void pitch_fader_update(void) {
    if (!s_state.initialized) {
        return;
    }
    
    // Read raw ADC value
    s_state.raw_value = read_adc_raw();
    
    // Apply EMA filter
    s_state.filtered_value = apply_filter(
        (float)s_state.raw_value,
        s_state.filtered_value,
        s_state.config.filter_alpha
    );
    
    // Convert to position
    s_state.position = raw_to_position(s_state.filtered_value, 
                                        &s_state.config.calibration);
    
    // Check if centered
    s_state.centered = (s_state.position == 0.0f);
    
    // Calculate pitch based on mode
    float range_max = PITCH_RANGE_VALUES[s_state.current_range];
    
    if (s_state.bend_mode) {
        // Bend mode: offset from base position
        float bend_amount = s_state.position - s_state.bend_base_position;
        s_state.pitch = bend_amount * range_max;
    } else {
        // Normal mode: direct mapping
        s_state.pitch = s_state.position * range_max;
    }
}

float pitch_fader_get_pitch(void) {
    return s_state.pitch;
}

uint16_t pitch_fader_get_raw(void) {
    return s_state.raw_value;
}

float pitch_fader_get_position(void) {
    return s_state.position;
}

bool pitch_fader_is_centered(void) {
    return s_state.centered;
}

void pitch_fader_set_range(pitch_range_t range) {
    if (range >= PITCH_RANGE_COUNT) {
        ESP_LOGW(TAG, "Invalid range: %d", range);
        return;
    }
    
    s_state.current_range = range;
    ESP_LOGI(TAG, "Range set to ±%.0f%%", PITCH_RANGE_VALUES[range]);
}

pitch_range_t pitch_fader_get_range(void) {
    return s_state.current_range;
}

float pitch_fader_get_range_max(void) {
    return PITCH_RANGE_VALUES[s_state.current_range];
}

void pitch_fader_cycle_range(void) {
    pitch_range_t next = (s_state.current_range + 1) % PITCH_RANGE_COUNT;
    pitch_fader_set_range(next);
}

/* ============================================================================
 * Calibration Implementation
 * ============================================================================ */

bool pitch_fader_calibration_start(void) {
    if (!s_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    s_state.calibrating = true;
    s_state.cal_min_set = false;
    s_state.cal_max_set = false;
    s_state.cal_center_set = false;
    
    ESP_LOGI(TAG, "Calibration started. Move fader to positions and capture.");
    return true;
}

void pitch_fader_calibration_set_min(void) {
    if (!s_state.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return;
    }
    
    s_state.cal_min_capture = s_state.raw_value;
    s_state.cal_min_set = true;
    ESP_LOGI(TAG, "Min captured: %u", s_state.cal_min_capture);
}

void pitch_fader_calibration_set_max(void) {
    if (!s_state.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return;
    }
    
    s_state.cal_max_capture = s_state.raw_value;
    s_state.cal_max_set = true;
    ESP_LOGI(TAG, "Max captured: %u", s_state.cal_max_capture);
}

void pitch_fader_calibration_set_center(void) {
    if (!s_state.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return;
    }
    
    s_state.cal_center_capture = s_state.raw_value;
    s_state.cal_center_set = true;
    ESP_LOGI(TAG, "Center captured: %u", s_state.cal_center_capture);
}

bool pitch_fader_calibration_finish(void) {
    if (!s_state.calibrating) {
        ESP_LOGW(TAG, "Not in calibration mode");
        return false;
    }
    
    // Validate all points captured
    if (!s_state.cal_min_set || !s_state.cal_max_set || !s_state.cal_center_set) {
        ESP_LOGE(TAG, "Calibration incomplete (min=%d, max=%d, center=%d)",
                 s_state.cal_min_set, s_state.cal_max_set, s_state.cal_center_set);
        return false;
    }
    
    // Validate order (min < center < max or inverted)
    bool normal_order = (s_state.cal_min_capture < s_state.cal_center_capture) &&
                        (s_state.cal_center_capture < s_state.cal_max_capture);
    bool inverted_order = (s_state.cal_max_capture < s_state.cal_center_capture) &&
                          (s_state.cal_center_capture < s_state.cal_min_capture);
    
    if (!normal_order && !inverted_order) {
        ESP_LOGE(TAG, "Invalid calibration order: min=%u, center=%u, max=%u",
                 s_state.cal_min_capture, s_state.cal_center_capture, 
                 s_state.cal_max_capture);
        s_state.calibrating = false;
        return false;
    }
    
    // Apply calibration
    if (inverted_order) {
        // Swap min/max and mark as inverted
        s_state.config.calibration.raw_min = s_state.cal_max_capture;
        s_state.config.calibration.raw_max = s_state.cal_min_capture;
        s_state.config.calibration.inverted = true;
    } else {
        s_state.config.calibration.raw_min = s_state.cal_min_capture;
        s_state.config.calibration.raw_max = s_state.cal_max_capture;
        s_state.config.calibration.inverted = false;
    }
    s_state.config.calibration.raw_center = s_state.cal_center_capture;
    
    // Calculate dead zone as ~2% of the smaller half-range
    uint16_t lower_range = s_state.config.calibration.raw_center - 
                           s_state.config.calibration.raw_min;
    uint16_t upper_range = s_state.config.calibration.raw_max - 
                           s_state.config.calibration.raw_center;
    uint16_t min_range = (lower_range < upper_range) ? lower_range : upper_range;
    s_state.config.calibration.dead_zone = min_range / 50; // ~2%
    
    // Ensure minimum dead zone
    if (s_state.config.calibration.dead_zone < 20) {
        s_state.config.calibration.dead_zone = 20;
    }
    
    s_state.calibrating = false;
    
    ESP_LOGI(TAG, "Calibration applied: min=%u, center=%u, max=%u, dead_zone=%u, inverted=%d",
             s_state.config.calibration.raw_min,
             s_state.config.calibration.raw_center,
             s_state.config.calibration.raw_max,
             s_state.config.calibration.dead_zone,
             s_state.config.calibration.inverted);
    
    return true;
}

void pitch_fader_get_calibration(pitch_fader_cal_t *cal) {
    if (cal) {
        *cal = s_state.config.calibration;
    }
}

bool pitch_fader_set_calibration(const pitch_fader_cal_t *cal) {
    if (!cal) {
        return false;
    }
    
    // Basic validation
    if (cal->raw_min >= cal->raw_max) {
        ESP_LOGE(TAG, "Invalid calibration: min >= max");
        return false;
    }
    
    if (cal->raw_center <= cal->raw_min || cal->raw_center >= cal->raw_max) {
        ESP_LOGE(TAG, "Invalid calibration: center out of range");
        return false;
    }
    
    s_state.config.calibration = *cal;
    
    ESP_LOGI(TAG, "Calibration set: min=%u, center=%u, max=%u, dead_zone=%u",
             cal->raw_min, cal->raw_center, cal->raw_max, cal->dead_zone);
    
    return true;
}

/* ============================================================================
 * Pitch Bend Implementation
 * ============================================================================ */

void pitch_fader_set_bend_mode(bool enable) {
    if (enable && !s_state.bend_mode) {
        // Entering bend mode - capture current position as base
        s_state.bend_base_position = s_state.position;
        ESP_LOGI(TAG, "Bend mode enabled (base: %.2f)", s_state.bend_base_position);
    } else if (!enable && s_state.bend_mode) {
        ESP_LOGI(TAG, "Bend mode disabled");
    }
    
    s_state.bend_mode = enable;
}

bool pitch_fader_is_bend_mode(void) {
    return s_state.bend_mode;
}

float pitch_fader_get_bend(void) {
    if (!s_state.bend_mode) {
        return 0.0f;
    }
    
    float range_max = PITCH_RANGE_VALUES[s_state.current_range];
    float bend_amount = s_state.position - s_state.bend_base_position;
    return bend_amount * range_max;
}
