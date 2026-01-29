/**
 * @file ui_manager.h
 * @brief High-Contrast HUD UI manager (LVGL-based)
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI view types
 */
typedef enum {
    UI_VIEW_WAVEFORM = 0,  ///< Main playback view with waveform
    UI_VIEW_CRATE,          ///< Library browser
    UI_VIEW_SETTINGS        ///< Settings menu
} ui_view_type_t;

/**
 * @brief UI color themes
 */
typedef enum {
    UI_THEME_AMBER = 0,    ///< Amber phosphor
    UI_THEME_CYAN,          ///< Cyber cyan
    UI_THEME_GREEN          ///< Radar green
} ui_theme_t;

/**
 * @brief Initialize UI system
 * 
 * @param width Screen width in pixels
 * @param height Screen height in pixels
 * @return 0 on success, negative on error
 */
int ui_manager_init(uint32_t width, uint32_t height);

/**
 * @brief Deinitialize UI system
 */
void ui_manager_deinit(void);

/**
 * @brief Set UI theme
 * 
 * @param theme Theme type
 */
void ui_manager_set_theme(ui_theme_t theme);

/**
 * @brief Switch to view
 * 
 * @param view View type
 */
void ui_manager_set_view(ui_view_type_t view);

/**
 * @brief Update waveform display
 * 
 * @param waveform_data Waveform peak data (array of values 0-255)
 * @param num_samples Number of samples
 * @param position Current playback position (0.0 to 1.0)
 * @param wave_index Current waveform buffer index for scroll optimization
 */
void ui_manager_update_waveform(const uint8_t *waveform_data, 
                                size_t num_samples, 
                                float position,
                                size_t wave_index);

/**
 * @brief Reset waveform scroll state (call when loading new track)
 */
void ui_manager_reset_waveform(void);

/**
 * @brief Update telemetry (BPM, pitch, phase error)
 * 
 * @param bpm Current BPM
 * @param pitch Pitch percentage (+/-)
 * @param phase_error Phase error (-1.0 to 1.0)
 */
void ui_manager_update_telemetry(float bpm, float pitch, float phase_error);

/**
 * @brief Update track metadata
 * 
 * @param title Track title
 * @param key Musical key
 * @param position Current position in seconds
 * @param duration Total duration in seconds
 */
void ui_manager_update_metadata(const char *title, const char *key, uint32_t position, uint32_t duration);

/**
 * @brief Update beat grid lines
 * 
 * @param beat_positions Array of beat positions (0.0 to 1.0)
 * @param num_beats Number of beats
 */
void ui_manager_update_grid(const float *beat_positions, size_t num_beats);

/**
 * @brief Show touch feedback (ghost cursor)
 * 
 * @param position Touch position (0.0 to 1.0)
 * @param visible True to show, false to hide
 */
void ui_manager_show_touch_feedback(float position, bool visible);

/**
 * @brief Process UI (call in main loop)
 */
void ui_manager_process(void);

/**
 * @brief Handle touch input
 * 
 * @param x X coordinate
 * @param y Y coordinate
 * @param pressed True if pressed, false if released
 */
void ui_manager_handle_touch(uint16_t x, uint16_t y, bool pressed);

/**
 * @brief Trigger nudge animation (waveform jerks left)
 */
void ui_manager_trigger_nudge_animation(void);

/**
 * @brief Handle crate selection event (track chosen)
 * 
 * @param index Index of selected track in database
 */
void ui_manager_handle_crate_select(int index);

/**
 * @brief Refresh crate view with latest tracks from database
 */
void ui_manager_refresh_crate(void);

/**
 * @brief Set waveform resolution for performance tuning
 * 
 * Higher divider = fewer bars = better performance, lower detail
 * 
 * @param divider Resolution divider (1=480 bars, 2=240, 4=120, 8=60)
 */
void ui_manager_set_waveform_resolution(int divider);

/**
 * @brief Get current waveform resolution divider
 * 
 * @return Current resolution divider (1-8)
 */
int ui_manager_get_waveform_resolution(void);

#ifdef __cplusplus
}
#endif

#endif // UI_MANAGER_H

