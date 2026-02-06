/**
 * @file loop_control.h
 * @brief Loop control interface with auto-loop, loop roll, and beat-quantized operations
 */

#ifndef LOOP_CONTROL_H
#define LOOP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Loop Size Definitions
// ============================================================================

/**
 * @brief Auto-loop beat sizes
 */
typedef enum {
    LOOP_SIZE_1_32 = 0,    ///< 1/32 beat (for loop rolls)
    LOOP_SIZE_1_16,        ///< 1/16 beat
    LOOP_SIZE_1_8,         ///< 1/8 beat
    LOOP_SIZE_1_4,         ///< 1/4 beat
    LOOP_SIZE_1_2,         ///< 1/2 beat
    LOOP_SIZE_1,           ///< 1 beat
    LOOP_SIZE_2,           ///< 2 beats
    LOOP_SIZE_4,           ///< 4 beats
    LOOP_SIZE_8,           ///< 8 beats
    LOOP_SIZE_16,          ///< 16 beats
    LOOP_SIZE_32,          ///< 32 beats
    LOOP_SIZE_COUNT
} loop_size_t;

/**
 * @brief Loop state
 */
typedef enum {
    LOOP_STATE_INACTIVE = 0,  ///< No loop active
    LOOP_STATE_ACTIVE,        ///< Normal loop active
    LOOP_STATE_ROLL           ///< Loop roll active (temporary)
} loop_state_t;

// ============================================================================
// Basic Loop Control (Legacy API - now uses ms internally)
// ============================================================================

/**
 * @brief Set loop in point
 * 
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool loop_control_set_in(uint32_t position);

/**
 * @brief Set loop out point
 * 
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool loop_control_set_out(uint32_t position);

/**
 * @brief Get loop in point
 * 
 * @return Position in seconds, or 0 if not set
 */
uint32_t loop_control_get_in(void);

/**
 * @brief Get loop out point
 * 
 * @return Position in seconds, or 0 if not set
 */
uint32_t loop_control_get_out(void);

/**
 * @brief Get loop length
 * 
 * @return Length in seconds, or 0 if loop not set
 */
uint32_t loop_control_get_length(void);

/**
 * @brief Check if loop is active
 * 
 * @return true if loop is set, false otherwise
 */
bool loop_control_is_active(void);

/**
 * @brief Clear loop
 */
void loop_control_clear(void);

// ============================================================================
// High-Precision API (milliseconds)
// ============================================================================

/**
 * @brief Set loop in point (milliseconds)
 * 
 * @param position_ms Position in milliseconds
 * @return true on success
 */
bool loop_control_set_in_ms(uint32_t position_ms);

/**
 * @brief Set loop out point (milliseconds)
 * 
 * @param position_ms Position in milliseconds
 * @return true on success
 */
bool loop_control_set_out_ms(uint32_t position_ms);

/**
 * @brief Get loop in point (milliseconds)
 * 
 * @return Position in milliseconds
 */
uint32_t loop_control_get_in_ms(void);

/**
 * @brief Get loop out point (milliseconds)
 * 
 * @return Position in milliseconds
 */
uint32_t loop_control_get_out_ms(void);

/**
 * @brief Get loop length (milliseconds)
 * 
 * @return Length in milliseconds
 */
uint32_t loop_control_get_length_ms(void);

// ============================================================================
// BPM Integration
// ============================================================================

/**
 * @brief Set the current BPM for beat calculations
 * 
 * Call this when BPM is detected or changes.
 * 
 * @param bpm Beats per minute (e.g., 128.0)
 */
void loop_control_set_bpm(float bpm);

/**
 * @brief Get current BPM used for calculations
 * 
 * @return Current BPM value
 */
float loop_control_get_bpm(void);

/**
 * @brief Calculate milliseconds per beat at current BPM
 * 
 * @return Milliseconds per beat
 */
uint32_t loop_control_get_ms_per_beat(void);

// ============================================================================
// Auto-Loop (Beat-Quantized)
// ============================================================================

/**
 * @brief Activate auto-loop at current position
 * 
 * Sets loop in at current position (quantized to nearest beat if possible)
 * and automatically sets loop out based on beat count.
 * 
 * @param size Loop size in beats
 * @param current_position_ms Current playback position in milliseconds
 * @return true on success
 */
bool loop_control_auto_loop(loop_size_t size, uint32_t current_position_ms);

/**
 * @brief Get the current auto-loop size
 * 
 * @return Current loop size, or LOOP_SIZE_4 as default
 */
loop_size_t loop_control_get_size(void);

/**
 * @brief Set auto-loop size (for next auto-loop activation)
 * 
 * @param size Loop size
 */
void loop_control_set_size(loop_size_t size);

/**
 * @brief Double the current loop length
 * 
 * Doubles the loop out point while keeping loop in fixed.
 * Maintains beat alignment.
 * 
 * @return true on success, false if no loop active or at max size
 */
bool loop_control_double(void);

/**
 * @brief Halve the current loop length
 * 
 * Halves the loop by moving loop out closer to loop in.
 * Maintains beat alignment.
 * 
 * @return true on success, false if no loop active or at min size
 */
bool loop_control_halve(void);

/**
 * @brief Get the beat multiplier for a loop size
 * 
 * @param size Loop size enum
 * @return Multiplier (e.g., 0.5 for half beat, 4.0 for 4 beats)
 */
float loop_control_size_to_beats(loop_size_t size);

/**
 * @brief Get display name for loop size
 * 
 * @param size Loop size enum
 * @return String like "1/2", "1", "4", "16"
 */
const char* loop_control_size_name(loop_size_t size);

// ============================================================================
// Loop Roll
// ============================================================================

/**
 * @brief Start loop roll
 * 
 * Creates a temporary loop at current position that will revert
 * when released. Background timeline continues.
 * 
 * @param size Roll size in beats
 * @param current_position_ms Current playback position
 * @return true on success
 */
bool loop_control_roll_start(loop_size_t size, uint32_t current_position_ms);

/**
 * @brief Release loop roll
 * 
 * Exits loop roll and returns to the position where playback
 * would be if the roll hadn't happened (background timeline).
 * 
 * @param elapsed_ms Time elapsed since roll started
 * @return Position to seek to (where background timeline is)
 */
uint32_t loop_control_roll_release(uint32_t elapsed_ms);

/**
 * @brief Check if loop roll is active
 * 
 * @return true if in loop roll mode
 */
bool loop_control_is_roll_active(void);

/**
 * @brief Get the background timeline position
 * 
 * During loop roll, returns where playback would be if not looping.
 * 
 * @param elapsed_ms Time since roll started
 * @return Background position in milliseconds
 */
uint32_t loop_control_get_background_position(uint32_t elapsed_ms);

// ============================================================================
// Loop Move/Shift
// ============================================================================

/**
 * @brief Move loop forward by beats
 * 
 * Shifts both loop in and out points forward by the specified beats.
 * 
 * @param beats Number of beats to move (can use loop_size_to_beats)
 * @return true on success
 */
bool loop_control_move_forward(float beats);

/**
 * @brief Move loop backward by beats
 * 
 * Shifts both loop in and out points backward by the specified beats.
 * 
 * @param beats Number of beats to move
 * @return true on success
 */
bool loop_control_move_backward(float beats);

/**
 * @brief Move loop by one loop-length forward
 * 
 * Convenience function to jump to next loop segment.
 * 
 * @return true on success
 */
bool loop_control_move_next(void);

/**
 * @brief Move loop by one loop-length backward
 * 
 * Convenience function to jump to previous loop segment.
 * 
 * @return true on success
 */
bool loop_control_move_prev(void);

// ============================================================================
// Loop State
// ============================================================================

/**
 * @brief Get current loop state
 * 
 * @return Current state (inactive, active, or roll)
 */
loop_state_t loop_control_get_state(void);

/**
 * @brief Toggle loop on/off
 * 
 * If loop points are set, toggles active state.
 * If no loop points, does nothing.
 * 
 * @return New active state
 */
bool loop_control_toggle(void);

/**
 * @brief Activate loop (if points are set)
 */
void loop_control_activate(void);

/**
 * @brief Deactivate loop (keeps points)
 */
void loop_control_deactivate(void);

// ============================================================================
// Playback Integration
// ============================================================================

/**
 * @brief Check if position should loop back
 * 
 * Call during playback to check if current position has passed loop out
 * and should jump back to loop in.
 * 
 * @param current_position_ms Current playback position
 * @return Position to seek to, or 0 if no seek needed
 */
uint32_t loop_control_check_loop_point(uint32_t current_position_ms);

#ifdef __cplusplus
}
#endif

#endif /* LOOP_CONTROL_H */
