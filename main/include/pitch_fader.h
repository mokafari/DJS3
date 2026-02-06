/**
 * @file pitch_fader.h
 * @brief Pitch fader (analog slider) interface for DJ controller
 * 
 * Provides ADC-based pitch fader reading with calibration, center detent
 * detection, range selection, and smooth filtering.
 */

#ifndef PITCH_FADER_H
#define PITCH_FADER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pitch range selection (typical DJ ranges)
 */
typedef enum {
    PITCH_RANGE_4  = 0,    /**< ±4% range (vinyl accuracy) */
    PITCH_RANGE_8  = 1,    /**< ±8% range (standard DJ) */
    PITCH_RANGE_16 = 2,    /**< ±16% range (extended) */
    PITCH_RANGE_50 = 3,    /**< ±50% range (wide/remix) */
    PITCH_RANGE_COUNT
} pitch_range_t;

/**
 * @brief Calibration data for pitch fader
 */
typedef struct {
    uint16_t raw_min;       /**< Raw ADC value at minimum position */
    uint16_t raw_max;       /**< Raw ADC value at maximum position */
    uint16_t raw_center;    /**< Raw ADC value at center detent */
    uint16_t dead_zone;     /**< Dead zone width around center (raw units) */
    bool     inverted;      /**< True if fader direction is inverted */
} pitch_fader_cal_t;

/**
 * @brief Pitch fader configuration
 */
typedef struct {
    int                adc_channel;     /**< ADC channel number (ADC1 only) */
    pitch_range_t      default_range;   /**< Default pitch range */
    float              filter_alpha;    /**< EMA filter coefficient (0.0-1.0, higher = less smoothing) */
    pitch_fader_cal_t  calibration;     /**< Calibration data */
} pitch_fader_config_t;

/**
 * @brief Get default configuration
 * 
 * @return Default configuration with typical values
 */
pitch_fader_config_t pitch_fader_get_default_config(void);

/**
 * @brief Initialize pitch fader
 * 
 * @param config Configuration (NULL for defaults)
 * @return true on success, false on failure
 */
bool pitch_fader_init(const pitch_fader_config_t *config);

/**
 * @brief Deinitialize pitch fader
 */
void pitch_fader_deinit(void);

/**
 * @brief Update pitch fader (call in main loop, ~10-50Hz recommended)
 * 
 * Reads ADC, applies filtering, and updates internal state.
 */
void pitch_fader_update(void);

/**
 * @brief Get current pitch percentage
 * 
 * Returns the filtered pitch value based on current range setting.
 * 
 * @return Pitch percentage (e.g., -8.0 to +8.0 for PITCH_RANGE_8)
 */
float pitch_fader_get_pitch(void);

/**
 * @brief Get raw ADC value (for calibration/debugging)
 * 
 * @return Raw ADC reading (0-4095 for 12-bit)
 */
uint16_t pitch_fader_get_raw(void);

/**
 * @brief Get normalized fader position
 * 
 * @return Position from -1.0 (min) to +1.0 (max), 0.0 at center
 */
float pitch_fader_get_position(void);

/**
 * @brief Check if fader is in center detent (dead zone)
 * 
 * @return true if in center dead zone
 */
bool pitch_fader_is_centered(void);

/**
 * @brief Set pitch range
 * 
 * @param range Pitch range selection
 */
void pitch_fader_set_range(pitch_range_t range);

/**
 * @brief Get current pitch range
 * 
 * @return Current pitch range
 */
pitch_range_t pitch_fader_get_range(void);

/**
 * @brief Get maximum pitch for current range
 * 
 * @return Maximum pitch percentage (e.g., 8.0 for ±8% range)
 */
float pitch_fader_get_range_max(void);

/**
 * @brief Cycle to next pitch range
 * 
 * Cycles through: ±4% → ±8% → ±16% → ±50% → ±4% ...
 */
void pitch_fader_cycle_range(void);

/* ============================================================================
 * Calibration Functions
 * ============================================================================ */

/**
 * @brief Start calibration mode
 * 
 * In calibration mode, raw values are captured for min/max/center.
 * 
 * @return true on success
 */
bool pitch_fader_calibration_start(void);

/**
 * @brief Capture current position as minimum
 */
void pitch_fader_calibration_set_min(void);

/**
 * @brief Capture current position as maximum
 */
void pitch_fader_calibration_set_max(void);

/**
 * @brief Capture current position as center
 */
void pitch_fader_calibration_set_center(void);

/**
 * @brief Finish calibration and apply values
 * 
 * @return true if calibration is valid, false if incomplete
 */
bool pitch_fader_calibration_finish(void);

/**
 * @brief Get current calibration data
 * 
 * @param cal Output calibration structure
 */
void pitch_fader_get_calibration(pitch_fader_cal_t *cal);

/**
 * @brief Set calibration data directly
 * 
 * @param cal Calibration data to apply
 * @return true on success
 */
bool pitch_fader_set_calibration(const pitch_fader_cal_t *cal);

/* ============================================================================
 * Pitch Bend Functions
 * ============================================================================ */

/**
 * @brief Enable/disable pitch bend mode
 * 
 * When enabled, the fader temporarily bends pitch from current value
 * rather than setting absolute pitch.
 * 
 * @param enable true to enable bend mode
 */
void pitch_fader_set_bend_mode(bool enable);

/**
 * @brief Check if bend mode is active
 * 
 * @return true if bend mode is enabled
 */
bool pitch_fader_is_bend_mode(void);

/**
 * @brief Get pitch bend amount
 * 
 * Only valid when bend mode is active.
 * 
 * @return Bend amount as percentage (-range to +range)
 */
float pitch_fader_get_bend(void);

#ifdef __cplusplus
}
#endif

#endif /* PITCH_FADER_H */
