/**
 * @file controls.h
 * @brief DJ deck control inputs (buttons, encoders)
 */

#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button IDs
 */
typedef enum {
    BUTTON_CUE = 0,
    BUTTON_PLAY_PAUSE,
    BUTTON_SYNC,
    BUTTON_LOOP_IN,
    BUTTON_LOOP_OUT,
    BUTTON_HOT_CUE_1,
    BUTTON_HOT_CUE_2,
    BUTTON_HOT_CUE_3,
    BUTTON_HOT_CUE_4,
    BUTTON_HOT_CUE_5,
    BUTTON_HOT_CUE_6,
    BUTTON_HOT_CUE_7,
    BUTTON_HOT_CUE_8,
    BUTTON_COUNT
} button_id_t;

/**
 * @brief Button event callback
 */
typedef void (*button_event_cb_t)(button_id_t button, bool pressed, void *arg);

/**
 * @brief Initialize control inputs
 * 
 * @param button_cb Button event callback (can be NULL)
 * @param arg User argument for callback
 * @return true on success, false on failure
 */
bool controls_init(button_event_cb_t button_cb, void *arg);

/**
 * @brief Deinitialize control inputs
 */
void controls_deinit(void);

/**
 * @brief Update controls (call in main loop)
 */
void controls_update(void);

/**
 * @brief Get button state
 * 
 * @param button Button ID
 * @return true if pressed, false if released
 */
bool controls_get_button(button_id_t button);

/**
 * @brief Get jog wheel delta (rotation since last call)
 * 
 * @return Rotation delta (-1, 0, or +1)
 */
int8_t controls_get_jog_delta(void);

/**
 * @brief Get jog wheel touch state
 * 
 * @return true if touched, false otherwise
 */
bool controls_get_jog_touch(void);

/**
 * @brief Get pitch encoder delta
 * 
 * @return Rotation delta (-1, 0, or +1)
 */
int8_t controls_get_pitch_delta(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLS_H */

