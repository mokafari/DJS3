/**
 * @file cue_points.h
 * @brief Cue point system interface with .odk persistence
 * 
 * Hot cue points are persisted to .odk metadata files so they
 * are restored automatically when a track is loaded.
 * 
 * Features:
 * - 8 hot cue slots (0-7)
 * - Cue+play mode (jump and start playback)
 * - Delete cue functionality
 * - Per-cue color customization
 * - Automatic .odk persistence
 */

#ifndef CUE_POINTS_H
#define CUE_POINTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CUE_POINTS 8

// Default cue colors (RGB565)
#define CUE_COLOR_RED    0xF800
#define CUE_COLOR_GREEN  0x07E0
#define CUE_COLOR_BLUE   0x001F
#define CUE_COLOR_YELLOW 0xFFE0
#define CUE_COLOR_ORANGE 0xFD20
#define CUE_COLOR_PURPLE 0xF81F
#define CUE_COLOR_CYAN   0x07FF
#define CUE_COLOR_WHITE  0xFFFF
#define CUE_COLOR_PINK   0xF81F  // Same as purple, could customize

/**
 * @brief Hot cue trigger mode
 */
typedef enum {
    CUE_MODE_JUMP = 0,      ///< Jump only (pauses if playing)
    CUE_MODE_PLAY,          ///< Jump and start playback
    CUE_MODE_PREVIEW        ///< Preview (play while held, return on release)
} cue_trigger_mode_t;

/**
 * @brief Initialize cue points for a track
 * 
 * Loads cue points from .odk metadata file if available.
 * Must be called when loading a new track.
 * 
 * @param filepath Path to MP3 file
 */
void cue_points_init_for_track(const char *filepath);

/**
 * @brief Set a cue point at position (in milliseconds)
 * 
 * Also persists to .odk metadata file (mutex-protected).
 * 
 * @param cue_index Cue index (0-7)
 * @param position_ms Position in milliseconds
 * @return true on success, false on failure
 */
bool cue_points_set_ms(uint8_t cue_index, uint32_t position_ms);

/**
 * @brief Set a cue point at current position (legacy, seconds)
 * 
 * @param cue_index Cue index (0-7)
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool cue_points_set(uint8_t cue_index, uint32_t position);

/**
 * @brief Get a cue point position in milliseconds
 * 
 * @param cue_index Cue index (0-7)
 * @return Position in milliseconds, or 0 if not set
 */
uint32_t cue_points_get_ms(uint8_t cue_index);

/**
 * @brief Get a cue point position (legacy, seconds)
 * 
 * @param cue_index Cue index (0-7)
 * @return Position in seconds, or 0 if not set
 */
uint32_t cue_points_get(uint8_t cue_index);

/**
 * @brief Check if a cue point is set
 * 
 * @param cue_index Cue index (0-7)
 * @return true if cue is set
 */
bool cue_points_is_set(uint8_t cue_index);

/**
 * @brief Get cue point color
 * 
 * @param cue_index Cue index (0-7)
 * @return RGB565 color value
 */
uint16_t cue_points_get_color(uint8_t cue_index);

/**
 * @brief Set cue point color
 * 
 * @param cue_index Cue index (0-7)
 * @param color RGB565 color value
 */
void cue_points_set_color(uint8_t cue_index, uint16_t color);

/**
 * @brief Clear a cue point
 * 
 * Also updates .odk metadata file.
 * 
 * @param cue_index Cue index (0-7)
 */
void cue_points_clear(uint8_t cue_index);

/**
 * @brief Clear all cue points
 */
void cue_points_clear_all(void);

/**
 * @brief Save cue points to metadata file
 * 
 * Called automatically by set/clear functions.
 * 
 * @return true on success
 */
bool cue_points_save(void);

/**
 * @brief Get default color for a cue index
 * 
 * @param cue_index Cue index (0-7)
 * @return RGB565 color value
 */
uint16_t cue_points_get_default_color(uint8_t cue_index);

/**
 * @brief Set global trigger mode for hot cues
 * 
 * @param mode Trigger mode (JUMP, PLAY, or PREVIEW)
 */
void cue_points_set_trigger_mode(cue_trigger_mode_t mode);

/**
 * @brief Get current trigger mode
 * 
 * @return Current trigger mode
 */
cue_trigger_mode_t cue_points_get_trigger_mode(void);

/**
 * @brief Trigger a hot cue (called when button pressed)
 * 
 * Behavior depends on trigger mode:
 * - JUMP: Seeks to cue position
 * - PLAY: Seeks and starts playback
 * - PREVIEW: Seeks and plays while button held
 * 
 * If cue is not set, sets it at current position.
 * 
 * @param cue_index Cue index (0-7)
 * @param current_position_ms Current playback position in ms
 * @return Position to seek to (ms), or 0 if cue was set
 */
uint32_t cue_points_trigger(uint8_t cue_index, uint32_t current_position_ms);

/**
 * @brief Release a hot cue (called when button released, for PREVIEW mode)
 * 
 * @param cue_index Cue index that was released
 */
void cue_points_release(uint8_t cue_index);

/**
 * @brief Delete a cue point (same as clear but with logging)
 * 
 * @param cue_index Cue index (0-7)
 * @return true if cue was deleted, false if it wasn't set
 */
bool cue_points_delete(uint8_t cue_index);

/**
 * @brief Cycle cue color to next preset
 * 
 * @param cue_index Cue index (0-7)
 * @return New color value
 */
uint16_t cue_points_cycle_color(uint8_t cue_index);

/**
 * @brief Get number of active cue points
 * 
 * @return Count of set cue points (0-8)
 */
uint8_t cue_points_get_active_count(void);

/**
 * @brief Get preview return position (for PREVIEW mode)
 * 
 * @return Position to return to when cue is released
 */
uint32_t cue_points_get_preview_return_position(void);

/**
 * @brief Check if preview mode is active
 * 
 * @return true if a cue is being previewed
 */
bool cue_points_is_preview_active(void);

/**
 * @brief Sync all active cue points to waveform display markers
 * 
 * Updates waveform markers to reflect current cue point state.
 * Call this after loading a track or modifying cue points.
 * 
 * @param track_duration_ms Track duration in milliseconds (for position scaling)
 */
void cue_points_sync_to_waveform(uint32_t track_duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* CUE_POINTS_H */

