/**
 * @file performance.h
 * @brief Performance mode controller interface
 * 
 * Controls entry/exit from performance mode with:
 * - Double-tap gesture detection
 * - Button/key mapping
 * - State synchronization
 */

#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Performance mode button IDs
 */
typedef enum {
    PERF_BUTTON_MODE = 0,   ///< Toggle performance mode
    PERF_BUTTON_SHIFT       ///< Shift modifier
} performance_button_t;

/**
 * @brief Initialize performance mode controller
 */
void performance_init(void);

/**
 * @brief Deinitialize performance mode controller
 */
void performance_deinit(void);

/**
 * @brief Enter performance mode
 */
void performance_enter(void);

/**
 * @brief Exit performance mode
 */
void performance_exit(void);

/**
 * @brief Toggle performance mode on/off
 */
void performance_toggle(void);

/**
 * @brief Check if performance mode is active
 * @return true if in performance mode
 */
bool performance_is_active(void);

/**
 * @brief Handle a tap event for gesture detection
 * 
 * Call this from touch input handler. Detects double-tap
 * gesture to enter performance mode.
 * 
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 * @return true if tap was consumed (e.g., triggered mode switch)
 */
bool performance_handle_tap(int x, int y);

/**
 * @brief Handle a button event
 * 
 * @param button Button ID
 * @param pressed true if pressed, false if released
 */
void performance_handle_button(performance_button_t button, bool pressed);

#ifdef __cplusplus
}
#endif

#endif // PERFORMANCE_H
