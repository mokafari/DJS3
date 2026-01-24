/**
 * @file pitch_control.h
 * @brief Pitch/tempo control interface
 */

#ifndef PITCH_CONTROL_H
#define PITCH_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize pitch control
 * 
 * @return true on success, false on failure
 */
bool pitch_control_init(void);

/**
 * @brief Set pitch adjustment percentage
 * 
 * @param pitch_percent Pitch adjustment (-50.0 to +50.0)
 * @return true on success, false on failure
 */
bool pitch_control_set(float pitch_percent);

/**
 * @brief Get current pitch adjustment
 * 
 * @return Pitch adjustment percentage (-50.0 to +50.0)
 */
float pitch_control_get(void);

/**
 * @brief Reset pitch to 0%
 */
void pitch_control_reset(void);

/**
 * @brief Update pitch control (call in main loop)
 */
void pitch_control_update(void);

#ifdef __cplusplus
}
#endif

#endif /* PITCH_CONTROL_H */

