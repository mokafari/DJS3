/**
 * @file mcpwm_encoder.h
 * @brief Fast encoder capture using MCPWM peripheral
 * 
 * For AS5600 magnetic encoder with PWM output.
 * MCPWM captures pulse width in hardware with zero CPU overhead.
 * 
 * Why MCPWM instead of I2C:
 * - I2C reads block for ~150us, causing audio glitches if called in ISR
 * - MCPWM capture reads angle from hardware register instantly
 * - AS5600 outputs PWM where Duty Cycle = Angle (0-360 degrees)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_attr.h"
#include "driver/mcpwm_cap.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pin where AS5600 PWM Output is connected (configure in board_config.h)
#ifndef ENCODER_CAPTURE_PIN
#define ENCODER_CAPTURE_PIN 4
#endif

/**
 * @brief Fast encoder handle
 */
typedef struct {
    mcpwm_cap_timer_handle_t cap_timer;     ///< MCPWM capture timer handle
    mcpwm_cap_channel_handle_t cap_chan;    ///< MCPWM capture channel handle
    uint32_t last_capture_val;              ///< Last captured pulse width
    uint32_t last_period;                    ///< Last captured period
    float current_angle;                     ///< 0.0 to 360.0 degrees
    float last_angle;                        ///< Previous angle (for velocity)
    uint32_t last_update_time_us;           ///< Timestamp of last update
    bool initialized;                        ///< Initialization flag
} fast_encoder_t;

/**
 * @brief Initialize MCPWM capture for encoder
 * 
 * Configures MCPWM peripheral to capture PWM signal from AS5600.
 * 
 * @param enc Pointer to encoder structure
 * @return ESP_OK on success, error code on failure
 */
esp_err_t encoder_init(fast_encoder_t* enc);

/**
 * @brief Get current angle (non-blocking, instant read)
 * 
 * Reads the last captured angle from the ISR-updated register.
 * Safe to call from audio processing loops.
 * 
 * @param enc Pointer to encoder structure
 * @return Angle in degrees (0.0 to 360.0)
 */
float IRAM_ATTR encoder_get_angle(fast_encoder_t* enc);

/**
 * @brief Get angular velocity (for scratch detection)
 * 
 * Calculates velocity based on angle change over time.
 * Useful for detecting scratch direction and speed.
 * 
 * @param enc Pointer to encoder structure
 * @return Velocity in degrees per second (positive = clockwise)
 */
float IRAM_ATTR encoder_get_velocity(fast_encoder_t* enc);

/**
 * @brief Get raw capture values (for debugging)
 * 
 * @param enc Pointer to encoder structure
 * @param pulse_width Output: last captured pulse width in timer ticks
 * @param period Output: last captured period in timer ticks
 */
void encoder_get_raw(fast_encoder_t* enc, uint32_t* pulse_width, uint32_t* period);

/**
 * @brief Deinitialize encoder capture
 * 
 * Frees MCPWM resources.
 * 
 * @param enc Pointer to encoder structure
 */
void encoder_deinit(fast_encoder_t* enc);

#ifdef __cplusplus
}
#endif
