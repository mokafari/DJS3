/**
 * @file slip_mode.h
 * @brief Slip Mode - Background timeline for non-destructive scratching/looping
 * 
 * Slip mode maintains a "background timeline" that continues advancing at
 * normal speed even while the DJ scratches, loops, or pauses. When slip mode
 * operations end, playback snaps back to where it "would have been" if the
 * DJ hadn't interfered.
 * 
 * This allows creative effects (scratches, loops, pauses) while maintaining
 * beat sync with other tracks or the master clock.
 * 
 * Usage:
 *   1. Enable slip mode: slip_mode_enable()
 *   2. Start a slip operation: slip_mode_begin_slip(SLIP_TRIGGER_SCRATCH)
 *   3. Background timeline continues while actual playback is manipulated
 *   4. End slip operation: slip_mode_end_slip()
 *   5. Playback snaps to background position
 */

#ifndef SLIP_MODE_H
#define SLIP_MODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Slip trigger types - what caused the slip
 */
typedef enum {
    SLIP_TRIGGER_NONE = 0,      ///< No active slip
    SLIP_TRIGGER_SCRATCH,       ///< Scratch/jog manipulation
    SLIP_TRIGGER_LOOP,          ///< Loop playback
    SLIP_TRIGGER_PAUSE,         ///< Pause while slip enabled
    SLIP_TRIGGER_HOT_CUE,       ///< Hot cue jump
    SLIP_TRIGGER_REVERSE        ///< Reverse playback
} slip_trigger_t;

/**
 * @brief Slip mode state structure
 */
typedef struct {
    bool enabled;                   ///< Slip mode globally enabled
    bool active;                    ///< Currently in a slip (manipulating playback)
    slip_trigger_t trigger;         ///< What triggered current slip
    
    // Background timeline (where playback "should" be)
    uint64_t background_position_bytes;     ///< Background position in bytes
    uint32_t background_start_time_ms;      ///< System time when slip started
    uint64_t slip_start_position_bytes;     ///< Position when slip started
    
    // Speed tracking
    float speed_ratio;              ///< Current speed ratio (1.0 = normal)
    uint32_t sample_rate;           ///< Current sample rate (44100, etc.)
    
    // Transition settings
    uint32_t snap_crossfade_ms;     ///< Crossfade duration when snapping back (0 = instant)
    bool snap_pending;              ///< Snap operation is pending
    uint64_t snap_target_bytes;     ///< Target position for snap
    float snap_progress;            ///< Crossfade progress (0.0 to 1.0)
} slip_mode_state_t;

// ============================================================================
// Core API
// ============================================================================

/**
 * @brief Initialize slip mode system
 * 
 * Call once during player initialization.
 */
void slip_mode_init(void);

/**
 * @brief Enable slip mode
 * 
 * When enabled, slip operations can be triggered. Background timeline
 * tracking is prepared but only active during actual slip operations.
 */
void slip_mode_enable(void);

/**
 * @brief Disable slip mode
 * 
 * Disabling while a slip is active will immediately end the slip
 * without snapping to background position.
 */
void slip_mode_disable(void);

/**
 * @brief Toggle slip mode on/off
 * 
 * @return true if now enabled, false if now disabled
 */
bool slip_mode_toggle(void);

/**
 * @brief Check if slip mode is enabled
 * 
 * @return true if slip mode is enabled
 */
bool slip_mode_is_enabled(void);

// ============================================================================
// Slip Operations
// ============================================================================

/**
 * @brief Begin a slip operation
 * 
 * Captures current playback position and starts background timeline.
 * Call when starting a scratch, loop, pause, etc.
 * 
 * @param trigger What triggered this slip
 * @param current_position_bytes Current playback position in bytes
 */
void slip_mode_begin_slip(slip_trigger_t trigger, uint64_t current_position_bytes);

/**
 * @brief End a slip operation and snap to background position
 * 
 * Calculates where playback "should" be and triggers a snap/seek.
 * If crossfade is configured, initiates smooth transition.
 * 
 * @return Target position in bytes to snap to
 */
uint64_t slip_mode_end_slip(void);

/**
 * @brief Check if currently in a slip operation
 * 
 * @return true if a slip is active
 */
bool slip_mode_is_active(void);

/**
 * @brief Get current slip trigger type
 * 
 * @return Current slip trigger, or SLIP_TRIGGER_NONE if not slipping
 */
slip_trigger_t slip_mode_get_trigger(void);

// ============================================================================
// Position Tracking
// ============================================================================

/**
 * @brief Update the background timeline
 * 
 * Call periodically (e.g., from playback task) to keep background
 * position current. Uses elapsed time and speed ratio to calculate.
 */
void slip_mode_update(void);

/**
 * @brief Set the current speed ratio for background timeline
 * 
 * Background timeline advances at this speed (affected by pitch fader).
 * 
 * @param speed_ratio Speed multiplier (1.0 = normal, 1.08 = +8%, etc.)
 */
void slip_mode_set_speed(float speed_ratio);

/**
 * @brief Set sample rate for position calculations
 * 
 * @param sample_rate Sample rate in Hz (e.g., 44100)
 */
void slip_mode_set_sample_rate(uint32_t sample_rate);

/**
 * @brief Get the current background position
 * 
 * Returns where playback "would be" if no slip was happening.
 * 
 * @return Background position in bytes
 */
uint64_t slip_mode_get_background_position(void);

/**
 * @brief Sync background position to actual playback position
 * 
 * Call when loading a new track or after a manual seek to reset
 * the background timeline to match actual position.
 * 
 * @param position_bytes Current playback position in bytes
 */
void slip_mode_sync_position(uint64_t position_bytes);

// ============================================================================
// Transition Control
// ============================================================================

/**
 * @brief Set crossfade duration for snap transitions
 * 
 * @param duration_ms Crossfade duration in milliseconds (0 = instant snap)
 */
void slip_mode_set_crossfade(uint32_t duration_ms);

/**
 * @brief Check if a snap transition is in progress
 * 
 * @return true if crossfading to background position
 */
bool slip_mode_is_snapping(void);

/**
 * @brief Get snap transition progress
 * 
 * @return Progress from 0.0 (start) to 1.0 (complete)
 */
float slip_mode_get_snap_progress(void);

/**
 * @brief Process snap transition
 * 
 * Call from playback task when snap_pending is true.
 * Updates snap progress and returns interpolated position.
 * 
 * @param current_position_bytes Current actual position
 * @param delta_time_ms Time elapsed since last call
 * @return Interpolated position to use for playback
 */
uint64_t slip_mode_process_snap(uint64_t current_position_bytes, uint32_t delta_time_ms);

// ============================================================================
// Integration Helpers
// ============================================================================

/**
 * @brief Should playback snap after this operation?
 * 
 * Helper to check if slip mode wants to take over position after
 * a scratch/loop/pause ends.
 * 
 * @return true if slip mode will provide the next position
 */
bool slip_mode_should_snap(void);

/**
 * @brief Get position to seek to after slip ends
 * 
 * Convenience function that combines end_slip logic with position return.
 * 
 * @param current_position_bytes Current position when slip ends
 * @return Position to seek to (background position), or current if slip not active
 */
uint64_t slip_mode_get_snap_target(uint64_t current_position_bytes);

/**
 * @brief Get the complete slip mode state (for debugging/UI)
 * 
 * @return Pointer to internal state structure (read-only)
 */
const slip_mode_state_t* slip_mode_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* SLIP_MODE_H */
