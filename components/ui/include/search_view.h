/**
 * @file search_view.h
 * @brief Search view interface for library filtering
 * 
 * Provides text search, BPM range filtering, and key compatibility
 * filtering for the track library.
 */

#ifndef SEARCH_VIEW_H
#define SEARCH_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback when a track is selected from search results
 * 
 * @param track_index Index of selected track in the database
 * @param user_data User data passed to callback
 */
typedef void (*search_track_selected_cb)(int track_index, void *user_data);

/**
 * @brief Callback when user exits search view
 * 
 * @param user_data User data passed to callback
 */
typedef void (*search_back_cb)(void *user_data);

/**
 * @brief Initialize search view
 * 
 * @param width Screen width
 * @param height Screen height
 */
void search_view_init(uint32_t width, uint32_t height);

/**
 * @brief Show search view
 */
void search_view_show(void);

/**
 * @brief Hide search view
 */
void search_view_hide(void);

/**
 * @brief Check if search view is visible
 * 
 * @return true if visible
 */
bool search_view_is_visible(void);

/**
 * @brief Set callback for track selection
 * 
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void search_view_set_track_callback(search_track_selected_cb cb, void *user_data);

/**
 * @brief Set callback for back/exit
 * 
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void search_view_set_back_callback(search_back_cb cb, void *user_data);

/**
 * @brief Set reference key for compatibility filter
 * 
 * When set, the key filter will show keys compatible with this reference.
 * 
 * @param key_id Camelot key ID (0-23), or 255 to disable
 */
void search_view_set_reference_key(uint8_t key_id);

/**
 * @brief Set reference BPM for range filter
 * 
 * When set, the BPM filter will center around this value.
 * 
 * @param bpm Reference BPM, or 0 to disable
 */
void search_view_set_reference_bpm(float bpm);

// ============================================================================
// Navigation (for hardware buttons/encoder)
// ============================================================================

/**
 * @brief Scroll up in results list
 */
void search_view_scroll_up(void);

/**
 * @brief Scroll down in results list
 */
void search_view_scroll_down(void);

/**
 * @brief Select current result or activate control
 */
void search_view_select(void);

/**
 * @brief Go back (close keyboard or exit search)
 */
void search_view_back(void);

/**
 * @brief Clear all search filters
 */
void search_view_clear_filters(void);

/**
 * @brief Cleanup resources
 */
void search_view_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // SEARCH_VIEW_H
