/**
 * @file settings_view.h
 * @brief Settings menu UI view with categories and sub-menus
 */

#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Settings categories
 */
typedef enum {
    SETTINGS_CAT_THEME = 0,    ///< Theme settings (color scheme, brightness)
    SETTINGS_CAT_AUDIO,        ///< Audio settings (output mode, volume)
    SETTINGS_CAT_DISPLAY,      ///< Display settings (resolution, timeout)
    SETTINGS_CAT_COUNT
} settings_category_t;

/**
 * @brief Callback for settings changes
 */
typedef void (*settings_changed_cb)(settings_category_t category, int setting_id, int value, void *user_data);

/**
 * @brief Callback for when user wants to exit settings
 */
typedef void (*settings_back_cb)(void *user_data);

/**
 * @brief Initialize settings view (call once during UI init)
 * 
 * @param width Screen width
 * @param height Screen height
 */
void settings_view_init(uint32_t width, uint32_t height);

/**
 * @brief Show settings view
 */
void settings_view_show(void);

/**
 * @brief Hide settings view
 */
void settings_view_hide(void);

/**
 * @brief Check if settings view is visible
 * 
 * @return true if visible
 */
bool settings_view_is_visible(void);

/**
 * @brief Refresh settings view with current values
 */
void settings_view_refresh(void);

/**
 * @brief Set callback for settings changes
 * 
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void settings_view_set_changed_callback(settings_changed_cb cb, void *user_data);

/**
 * @brief Set callback for back/exit
 * 
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void settings_view_set_back_callback(settings_back_cb cb, void *user_data);

// ============================================================================
// Navigation (for hardware buttons/encoder)
// ============================================================================

/**
 * @brief Scroll up in current menu
 */
void settings_view_scroll_up(void);

/**
 * @brief Scroll down in current menu
 */
void settings_view_scroll_down(void);

/**
 * @brief Select current item or enter sub-menu
 */
void settings_view_select(void);

/**
 * @brief Go back to parent menu or exit settings
 */
void settings_view_back(void);

/**
 * @brief Adjust current value (for value items)
 * 
 * @param delta Change amount (+1 increase, -1 decrease)
 */
void settings_view_adjust_value(int delta);

// ============================================================================
// Current Settings Access
// ============================================================================

/**
 * @brief Get current theme index (0=Amber, 1=Cyan, 2=Green)
 */
int settings_get_theme(void);

/**
 * @brief Get current brightness level (0-255)
 */
int settings_get_brightness(void);

/**
 * @brief Get current volume level (0-100)
 */
int settings_get_volume(void);

/**
 * @brief Get current audio mode (0=Simple, 1=Granular)
 */
int settings_get_audio_mode(void);

/**
 * @brief Get waveform resolution divider (1, 2, 4, 8)
 */
int settings_get_waveform_resolution(void);

/**
 * @brief Get screen timeout in seconds (0=disabled)
 */
int settings_get_screen_timeout(void);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_VIEW_H
