/**
 * @file performance_view.h
 * @brief Performance mode view - Simplified UI for live DJ use
 * 
 * Optimized for club environments with:
 * - Maximized waveform display
 * - Essential controls only (play/pause, cue, loop)
 * - High contrast for dark environments
 * - Large touch targets for reliability
 */

#ifndef PERFORMANCE_VIEW_H
#define PERFORMANCE_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exit callback type
 * 
 * Called when user exits performance mode (to return to normal view)
 */
typedef void (*performance_view_exit_cb_t)(void);

/**
 * @brief Initialize performance view
 * 
 * Creates the performance mode UI with maximized waveform and minimal controls.
 * Must be called after LVGL and theme initialization.
 * 
 * @param width Screen width in pixels
 * @param height Screen height in pixels
 */
void performance_view_init(uint32_t width, uint32_t height);

/**
 * @brief Deinitialize performance view and free resources
 */
void performance_view_deinit(void);

/**
 * @brief Show performance view
 * 
 * Switches to performance mode, hiding other views.
 * Syncs state with audio player and cue markers.
 */
void performance_view_show(void);

/**
 * @brief Hide performance view
 * 
 * Returns control to normal view mode.
 */
void performance_view_hide(void);

/**
 * @brief Check if performance view is visible
 * 
 * @return true if performance mode is active
 */
bool performance_view_is_visible(void);

/**
 * @brief Update performance view with waveform data
 * 
 * Should be called each frame during playback with current waveform data.
 * Handles frame throttling internally for optimal performance.
 * 
 * @param waveform_data Waveform peak data (480 samples expected)
 * @param num_samples Number of samples in waveform_data
 * @param position Current playback position (0.0 to 1.0)
 * @param precise_time Current time in seconds
 * @param wave_index Current waveform buffer index for scroll optimization
 */
void performance_view_update(const uint8_t *waveform_data, size_t num_samples,
                              float position, float precise_time, size_t wave_index);

/**
 * @brief Reset performance view state
 * 
 * Call when loading a new track. Clears waveform cache and loop markers.
 */
void performance_view_reset(void);

/**
 * @brief Set exit callback
 * 
 * Register a callback to be called when user exits performance mode.
 * Typically used to switch back to the normal waveform view.
 * 
 * @param callback Function to call on exit, or NULL to clear
 */
void performance_view_set_exit_callback(performance_view_exit_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif // PERFORMANCE_VIEW_H
