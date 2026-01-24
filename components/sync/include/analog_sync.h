/**
 * @file analog_sync.h
 * @brief Analog sync pulse output (for Korg Volcas, Pocket Operators, etc.)
 */

#ifndef ANALOG_SYNC_H
#define ANALOG_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Analog sync handle
 */
typedef struct analog_sync_s analog_sync_t;

/**
 * @brief Create analog sync instance
 * 
 * @param gpio_pin GPIO pin for sync pulse output
 * @return Analog sync handle or NULL on error
 */
analog_sync_t *analog_sync_create(int gpio_pin);

/**
 * @brief Destroy analog sync instance
 * 
 * @param sync Analog sync handle
 */
void analog_sync_destroy(analog_sync_t *sync);

/**
 * @brief Set BPM
 * 
 * @param sync Analog sync handle
 * @param bpm BPM value (60-180)
 */
void analog_sync_set_bpm(analog_sync_t *sync, float bpm);

/**
 * @brief Set swing amount (delay every second pulse)
 * 
 * @param sync Analog sync handle
 * @param swing_ms Swing delay in milliseconds (0-50)
 */
void analog_sync_set_swing(analog_sync_t *sync, float swing_ms);

/**
 * @brief Start sync pulses
 * 
 * @param sync Analog sync handle
 */
void analog_sync_start(analog_sync_t *sync);

/**
 * @brief Stop sync pulses
 * 
 * @param sync Analog sync handle
 */
void analog_sync_stop(analog_sync_t *sync);

#ifdef __cplusplus
}
#endif

#endif // ANALOG_SYNC_H

