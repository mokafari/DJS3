/**
 * @file synth_mode.h
 * @brief Synth mode control mapping for granular engine parameters
 * 
 * When synth mode is active, controls are remapped:
 * - Pitch Fader → Grain Size (metallic to chunk)
 * - Touch Strip → Density/Overlap (stutter to lush)
 * - Nudge Buttons → Jitter (momentary glitch)
 * - Shift + Controls → Window function selection
 */

#ifndef SYNTH_MODE_H
#define SYNTH_MODE_H

#include <stdbool.h>
#include <stdint.h>
#include "granular_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Synth mode state
 */
typedef struct {
    bool active;                    ///< Is synth mode currently active?
    granular_params_t saved_params; ///< Saved normal mode parameters
    granular_params_t synth_params;  ///< Current synth mode parameters
} synth_mode_t;

/**
 * @brief Initialize synth mode
 * 
 * @param synth Synth mode handle
 * @return 0 on success, negative on error
 */
int synth_mode_init(synth_mode_t *synth);

/**
 * @brief Enable/disable synth mode
 * 
 * @param synth Synth mode handle
 * @param enabled True to enable synth mode
 * @param current_params Current granular parameters (to save)
 */
void synth_mode_set_active(synth_mode_t *synth, bool enabled, 
                          const granular_params_t *current_params);

/**
 * @brief Check if synth mode is active
 * 
 * @param synth Synth mode handle
 * @return True if active
 */
bool synth_mode_is_active(const synth_mode_t *synth);

/**
 * @brief Map pitch fader value to grain size
 * 
 * @param synth Synth mode handle
 * @param pitch_value Pitch fader value (-50.0 to +50.0)
 * @return Grain size in milliseconds (10-200ms)
 */
float synth_mode_map_pitch_to_grain_size(const synth_mode_t *synth, float pitch_value);

/**
 * @brief Map touch strip position to density
 * 
 * @param synth Synth mode handle
 * @param strip_position Touch strip position (0.0 to 1.0)
 * @return Density percentage (25-300%)
 */
float synth_mode_map_strip_to_density(const synth_mode_t *synth, float strip_position);

/**
 * @brief Trigger momentary jitter (from nudge button)
 * 
 * @param synth Synth mode handle
 * @param jitter_amount Jitter amount in milliseconds (0-50ms)
 */
void synth_mode_trigger_jitter(synth_mode_t *synth, float jitter_amount);

/**
 * @brief Update synth mode parameters from controls
 * 
 * @param synth Synth mode handle
 * @param pitch_value Pitch fader value (-50.0 to +50.0)
 * @param strip_position Touch strip position (0.0 to 1.0)
 * @param params Output parameters structure
 */
void synth_mode_update_params(synth_mode_t *synth, 
                              float pitch_value, 
                              float strip_position,
                              granular_params_t *params);

/**
 * @brief Cycle window function
 * 
 * @param synth Synth mode handle
 * @return New window function
 */
granular_window_t synth_mode_cycle_window(synth_mode_t *synth);

/**
 * @brief Get current synth mode parameters
 * 
 * @param synth Synth mode handle
 * @return Current parameters
 */
const granular_params_t* synth_mode_get_params(const synth_mode_t *synth);

#ifdef __cplusplus
}
#endif

#endif // SYNTH_MODE_H

