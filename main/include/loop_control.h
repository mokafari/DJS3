/**
 * @file loop_control.h
 * @brief Loop control interface
 */

#ifndef LOOP_CONTROL_H
#define LOOP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set loop in point at current position
 * 
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool loop_control_set_in(uint32_t position);

/**
 * @brief Set loop out point at current position
 * 
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool loop_control_set_out(uint32_t position);

/**
 * @brief Get loop in point
 * 
 * @return Position in seconds, or 0 if not set
 */
uint32_t loop_control_get_in(void);

/**
 * @brief Get loop out point
 * 
 * @return Position in seconds, or 0 if not set
 */
uint32_t loop_control_get_out(void);

/**
 * @brief Get loop length
 * 
 * @return Length in seconds, or 0 if loop not set
 */
uint32_t loop_control_get_length(void);

/**
 * @brief Check if loop is active
 * 
 * @return true if loop is set, false otherwise
 */
bool loop_control_is_active(void);

/**
 * @brief Clear loop
 */
void loop_control_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* LOOP_CONTROL_H */

