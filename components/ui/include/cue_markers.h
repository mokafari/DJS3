/**
 * @file cue_markers.h
 * @brief Cue markers UI overlay for waveform display
 * 
 * Renders colored triangular/diamond markers on the waveform to indicate:
 * - Hot cues (instant jump points, typically 4-8)
 * - Memory cues (saved positions)
 * - Loop markers (loop in/out points)
 */

#ifndef CUE_MARKERS_H
#define CUE_MARKERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

#define CUE_MARKERS_MAX_HOT_CUES     8   ///< Maximum hot cue slots
#define CUE_MARKERS_MAX_MEMORY_CUES  16  ///< Maximum memory cue slots
#define CUE_MARKERS_LABEL_MAX_LEN    4   ///< Max label length (e.g., "A", "1", "IN")

// ============================================================================
// TYPES
// ============================================================================

/**
 * @brief Cue marker type
 */
typedef enum {
    CUE_TYPE_HOT_CUE = 0,    ///< Hot cue (instant jump, colored)
    CUE_TYPE_MEMORY_CUE,     ///< Memory cue (saved position, white)
    CUE_TYPE_LOOP_IN,        ///< Loop start point (green)
    CUE_TYPE_LOOP_OUT,       ///< Loop end point (yellow)
    CUE_TYPE_TEMP_CUE        ///< Temporary cue (preview, dimmed)
} cue_marker_type_t;

/**
 * @brief Cue marker color preset
 */
typedef enum {
    CUE_COLOR_RED = 0,
    CUE_COLOR_ORANGE,
    CUE_COLOR_YELLOW,
    CUE_COLOR_GREEN,
    CUE_COLOR_CYAN,
    CUE_COLOR_BLUE,
    CUE_COLOR_PURPLE,
    CUE_COLOR_PINK,
    CUE_COLOR_WHITE,        ///< For memory cues
    CUE_COLOR_CUSTOM        ///< Use custom RGB
} cue_marker_color_t;

/**
 * @brief Single cue marker definition
 */
typedef struct {
    bool active;                            ///< Marker is in use
    float position;                         ///< Position in track (0.0 to 1.0)
    float position_seconds;                 ///< Position in seconds (for precise display)
    cue_marker_type_t type;                 ///< Marker type
    cue_marker_color_t color_preset;        ///< Color preset
    lv_color_t custom_color;                ///< Custom color (if CUE_COLOR_CUSTOM)
    char label[CUE_MARKERS_LABEL_MAX_LEN + 1]; ///< Label text (e.g., "1", "A", "IN")
    bool selected;                          ///< Currently selected/highlighted
    bool triggered;                         ///< Recently triggered (for animation)
} cue_marker_t;

/**
 * @brief Loop region definition
 */
typedef struct {
    bool active;                ///< Loop is set
    float start_position;       ///< Loop start (0.0 to 1.0)
    float end_position;         ///< Loop end (0.0 to 1.0)
    float start_seconds;        ///< Start in seconds
    float end_seconds;          ///< End in seconds
    bool enabled;               ///< Loop playback is active
} cue_loop_region_t;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize cue markers overlay
 * 
 * Creates the marker overlay on top of the waveform container.
 * Must be called after waveform_view_init().
 * 
 * @param parent Parent LVGL object (typically waveform container)
 * @param width Width of overlay
 * @param height Height of overlay
 */
void cue_markers_init(lv_obj_t *parent, uint32_t width, uint32_t height);

/**
 * @brief Deinitialize cue markers and free resources
 */
void cue_markers_deinit(void);

/**
 * @brief Reset all markers (call when loading new track)
 */
void cue_markers_reset(void);

// ============================================================================
// HOT CUE MANAGEMENT
// ============================================================================

/**
 * @brief Set a hot cue at slot
 * 
 * @param slot Hot cue slot (0 to CUE_MARKERS_MAX_HOT_CUES-1)
 * @param position Position in track (0.0 to 1.0)
 * @param position_seconds Position in seconds
 * @param color Color preset
 * @param label Optional label (NULL for default "1", "2", etc.)
 * @return true if set successfully
 */
bool cue_markers_set_hot_cue(int slot, float position, float position_seconds,
                              cue_marker_color_t color, const char *label);

/**
 * @brief Clear a hot cue slot
 * 
 * @param slot Hot cue slot to clear
 */
void cue_markers_clear_hot_cue(int slot);

/**
 * @brief Get hot cue at slot
 * 
 * @param slot Hot cue slot
 * @return Pointer to marker, or NULL if not set
 */
const cue_marker_t* cue_markers_get_hot_cue(int slot);

/**
 * @brief Trigger hot cue animation (flash effect)
 * 
 * @param slot Hot cue slot that was triggered
 */
void cue_markers_trigger_hot_cue(int slot);

// ============================================================================
// MEMORY CUE MANAGEMENT
// ============================================================================

/**
 * @brief Add a memory cue at position
 * 
 * @param position Position in track (0.0 to 1.0)
 * @param position_seconds Position in seconds
 * @param label Optional label
 * @return Index of created cue, or -1 if full
 */
int cue_markers_add_memory_cue(float position, float position_seconds, 
                                const char *label);

/**
 * @brief Remove memory cue at index
 * 
 * @param index Memory cue index
 */
void cue_markers_remove_memory_cue(int index);

/**
 * @brief Get number of active memory cues
 * 
 * @return Number of memory cues
 */
int cue_markers_get_memory_cue_count(void);

// ============================================================================
// LOOP MARKERS
// ============================================================================

/**
 * @brief Set loop region
 * 
 * @param start_position Loop start (0.0 to 1.0)
 * @param end_position Loop end (0.0 to 1.0)
 * @param start_seconds Start in seconds
 * @param end_seconds End in seconds
 */
void cue_markers_set_loop(float start_position, float end_position,
                          float start_seconds, float end_seconds);

/**
 * @brief Clear loop region
 */
void cue_markers_clear_loop(void);

/**
 * @brief Enable/disable loop playback
 * 
 * @param enabled True to enable loop
 */
void cue_markers_set_loop_enabled(bool enabled);

/**
 * @brief Get current loop region
 * 
 * @return Pointer to loop region, or NULL if not set
 */
const cue_loop_region_t* cue_markers_get_loop(void);

// ============================================================================
// RENDERING
// ============================================================================

/**
 * @brief Update marker display
 * 
 * Should be called each frame with current playback position.
 * Updates marker visibility based on zoom/scroll and highlights active cue.
 * 
 * @param playback_position Current playback position (0.0 to 1.0)
 * @param playback_seconds Current playback time in seconds
 * @param zoom_level Current zoom level (1.0 = no zoom)
 * @param center_time Time at center of waveform view (seconds)
 */
void cue_markers_update(float playback_position, float playback_seconds,
                        float zoom_level, float center_time);

/**
 * @brief Force redraw of all markers
 */
void cue_markers_invalidate(void);

/**
 * @brief Show/hide marker overlay
 * 
 * @param visible True to show
 */
void cue_markers_set_visible(bool visible);

// ============================================================================
// SELECTION / INTERACTION
// ============================================================================

/**
 * @brief Select marker at position (for editing/deletion)
 * 
 * @param position Touch position (0.0 to 1.0)
 * @param tolerance Selection tolerance in normalized units
 * @return Index of selected marker, or -1 if none
 */
int cue_markers_select_at_position(float position, float tolerance);

/**
 * @brief Clear current selection
 */
void cue_markers_clear_selection(void);

/**
 * @brief Get currently selected marker
 * 
 * @return Pointer to selected marker, or NULL
 */
const cue_marker_t* cue_markers_get_selected(void);

// ============================================================================
// COLOR UTILITIES
// ============================================================================

/**
 * @brief Get LVGL color for preset
 * 
 * @param preset Color preset
 * @return LVGL color value
 */
lv_color_t cue_markers_get_preset_color(cue_marker_color_t preset);

#ifdef __cplusplus
}
#endif

#endif // CUE_MARKERS_H
