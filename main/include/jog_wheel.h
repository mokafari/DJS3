/**
 * @file jog_wheel.h
 * @brief Touch-sensitive jog wheel controller interface
 * 
 * Provides comprehensive jog wheel control with:
 * - Capacitive touch detection for scratch vs nudge mode switching
 * - High-resolution rotary encoder velocity calculation
 * - Scratch mode - direct audio manipulation with vinyl feel
 * - Nudge mode - fine tempo adjustment without touching
 * - Search mode - fast track seeking for navigation
 * - Touch sensitivity calibration
 * - Configurable response curves for different DJ styles
 * - Integration with slip mode for non-destructive scratching
 */

#ifndef JOG_WHEEL_H
#define JOG_WHEEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Velocity history buffer size for smoothing */
#define JOG_VELOCITY_HISTORY_SIZE   8

/** Number of response curve points */
#define JOG_CURVE_POINTS            5

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Jog wheel operating modes
 */
typedef enum {
    JOG_MODE_VINYL = 0,      /**< Vinyl mode: touch=scratch, no-touch=nudge */
    JOG_MODE_CDJ,            /**< CDJ mode: touch=scratch, no-touch=seek */
    JOG_MODE_NUDGE_ONLY,     /**< Nudge only: always nudge regardless of touch */
    JOG_MODE_SEARCH,         /**< Search mode: fast track seeking */
    JOG_MODE_DISABLED,       /**< Jog wheel disabled */
    JOG_MODE_COUNT
} jog_mode_t;

/**
 * @brief Jog wheel action type (what the jog is currently doing)
 */
typedef enum {
    JOG_ACTION_NONE = 0,     /**< No action (wheel stationary or disabled) */
    JOG_ACTION_SCRATCH,      /**< Scratching (touch + rotation) */
    JOG_ACTION_NUDGE,        /**< Nudging tempo (no touch + rotation) */
    JOG_ACTION_SEARCH,       /**< Seeking through track */
    JOG_ACTION_BRAKE,        /**< Brake effect (touch without rotation) */
} jog_action_t;

/**
 * @brief Response curve types
 */
typedef enum {
    JOG_CURVE_LINEAR = 0,    /**< Linear response */
    JOG_CURVE_EXPONENTIAL,   /**< Exponential (more sensitive at high speeds) */
    JOG_CURVE_LOGARITHMIC,   /**< Logarithmic (more sensitive at low speeds) */
    JOG_CURVE_S_CURVE,       /**< S-curve (fine control at extremes) */
    JOG_CURVE_CUSTOM,        /**< User-defined curve points */
    JOG_CURVE_COUNT
} jog_curve_t;

/**
 * @brief Touch sensor calibration data
 */
typedef struct {
    uint16_t threshold_on;       /**< Touch detected threshold */
    uint16_t threshold_off;      /**< Touch released threshold (hysteresis) */
    uint16_t baseline;           /**< Baseline reading (no touch) */
    uint32_t sample_count;       /**< Samples for averaging */
    bool inverted;               /**< Invert touch logic */
} jog_touch_cal_t;

/**
 * @brief Jog wheel sensitivity settings
 */
typedef struct {
    float scratch_sensitivity;   /**< Scratch mode sensitivity (0.1-10.0, default 1.0) */
    float nudge_sensitivity;     /**< Nudge mode sensitivity (0.1-10.0, default 1.0) */
    float search_sensitivity;    /**< Search mode sensitivity (0.1-10.0, default 1.0) */
    float dead_zone;             /**< Minimum rotation to register (0.0-0.5) */
    float brake_strength;        /**< Brake effect strength (0.0-1.0) */
} jog_sensitivity_t;

/**
 * @brief Custom response curve definition
 */
typedef struct {
    float input[JOG_CURVE_POINTS];    /**< Input velocity points (0.0-1.0) */
    float output[JOG_CURVE_POINTS];   /**< Output velocity points (0.0-1.0) */
} jog_custom_curve_t;

/**
 * @brief Jog wheel configuration
 */
typedef struct {
    jog_mode_t default_mode;           /**< Default operating mode */
    jog_curve_t response_curve;        /**< Response curve type */
    jog_sensitivity_t sensitivity;     /**< Sensitivity settings */
    jog_touch_cal_t touch_cal;         /**< Touch sensor calibration */
    jog_custom_curve_t custom_curve;   /**< Custom curve (if CURVE_CUSTOM) */
    
    // Encoder settings
    uint16_t pulses_per_revolution;    /**< Encoder resolution (PPR) */
    bool encoder_inverted;             /**< Invert encoder direction */
    
    // Timing
    uint32_t velocity_update_ms;       /**< Velocity calculation interval */
    uint32_t touch_debounce_ms;        /**< Touch debounce time */
    
    // Integration
    bool slip_mode_integration;        /**< Auto-trigger slip mode on scratch */
    bool auto_brake_on_touch;          /**< Apply brake effect on initial touch */
} jog_config_t;

/**
 * @brief Jog wheel state information
 */
typedef struct {
    // Current action
    jog_mode_t mode;                 /**< Current operating mode */
    jog_action_t action;             /**< Current action being performed */
    
    // Touch state
    bool touched;                    /**< Wheel is currently touched */
    uint32_t touch_start_time;       /**< When touch started (ms) */
    uint32_t touch_duration;         /**< How long touched (ms) */
    
    // Rotation state
    int32_t position;                /**< Cumulative encoder position */
    int32_t delta;                   /**< Position change since last update */
    float velocity;                  /**< Current angular velocity (normalized) */
    float velocity_raw;              /**< Raw velocity before curve */
    float direction;                 /**< -1.0 (CCW) to +1.0 (CW) */
    
    // Output values
    float scratch_value;             /**< Scratch playback speed (-2.0 to +2.0) */
    float nudge_value;               /**< Tempo nudge amount (-1.0 to +1.0) */
    float search_value;              /**< Seek amount (seconds per update) */
    float brake_value;               /**< Brake effect amount (0.0 to 1.0) */
    
    // Statistics
    uint32_t update_count;           /**< Total updates processed */
    uint32_t scratch_count;          /**< Number of scratch sessions */
    float peak_velocity;             /**< Peak velocity recorded */
} jog_state_t;

/**
 * @brief Jog wheel event callback
 */
typedef void (*jog_event_cb_t)(jog_action_t action, float value, void *arg);

/**
 * @brief Touch event callback
 */
typedef void (*jog_touch_cb_t)(bool touched, void *arg);

/* ============================================================================
 * Initialization & Configuration
 * ============================================================================ */

/**
 * @brief Get default jog wheel configuration
 * 
 * @return Default configuration with typical DJ values
 */
jog_config_t jog_wheel_get_default_config(void);

/**
 * @brief Initialize jog wheel controller
 * 
 * @param config Configuration (NULL for defaults)
 * @return true on success, false on failure
 */
bool jog_wheel_init(const jog_config_t *config);

/**
 * @brief Deinitialize jog wheel controller
 */
void jog_wheel_deinit(void);

/**
 * @brief Set jog wheel callbacks
 * 
 * @param event_cb Action event callback (scratch/nudge/search)
 * @param touch_cb Touch state callback
 * @param arg User argument passed to callbacks
 */
void jog_wheel_set_callbacks(jog_event_cb_t event_cb, jog_touch_cb_t touch_cb, void *arg);

/* ============================================================================
 * Update & Processing
 * ============================================================================ */

/**
 * @brief Update jog wheel state (call in main loop)
 * 
 * Reads encoder, processes touch, calculates velocity, and triggers callbacks.
 * Recommended update rate: 100-500Hz for smooth scratching.
 */
void jog_wheel_update(void);

/**
 * @brief Process raw encoder input
 * 
 * Call this with raw encoder delta for external encoder input.
 * 
 * @param delta Raw encoder delta value
 */
void jog_wheel_process_encoder(int8_t delta);

/**
 * @brief Process raw touch input
 * 
 * Call this with raw touch reading for external capacitive sensor.
 * 
 * @param raw_value Raw capacitive reading
 */
void jog_wheel_process_touch(uint16_t raw_value);

/* ============================================================================
 * Mode Control
 * ============================================================================ */

/**
 * @brief Set jog wheel operating mode
 * 
 * @param mode New operating mode
 */
void jog_wheel_set_mode(jog_mode_t mode);

/**
 * @brief Get current operating mode
 * 
 * @return Current mode
 */
jog_mode_t jog_wheel_get_mode(void);

/**
 * @brief Cycle through operating modes
 */
void jog_wheel_cycle_mode(void);

/**
 * @brief Temporarily enable search mode
 * 
 * Useful for holding shift+jog to seek.
 * 
 * @param enable Enable or disable search override
 */
void jog_wheel_search_mode(bool enable);

/* ============================================================================
 * Sensitivity & Response
 * ============================================================================ */

/**
 * @brief Set scratch sensitivity
 * 
 * @param sensitivity Sensitivity multiplier (0.1-10.0)
 */
void jog_wheel_set_scratch_sensitivity(float sensitivity);

/**
 * @brief Set nudge sensitivity
 * 
 * @param sensitivity Sensitivity multiplier (0.1-10.0)
 */
void jog_wheel_set_nudge_sensitivity(float sensitivity);

/**
 * @brief Set search sensitivity
 * 
 * @param sensitivity Sensitivity multiplier (0.1-10.0)
 */
void jog_wheel_set_search_sensitivity(float sensitivity);

/**
 * @brief Set response curve type
 * 
 * @param curve Curve type
 */
void jog_wheel_set_curve(jog_curve_t curve);

/**
 * @brief Set custom response curve
 * 
 * @param curve Custom curve definition
 */
void jog_wheel_set_custom_curve(const jog_custom_curve_t *curve);

/**
 * @brief Set dead zone
 * 
 * @param dead_zone Dead zone amount (0.0-0.5)
 */
void jog_wheel_set_dead_zone(float dead_zone);

/**
 * @brief Set brake effect strength
 * 
 * @param strength Brake strength (0.0-1.0)
 */
void jog_wheel_set_brake_strength(float strength);

/* ============================================================================
 * State Access
 * ============================================================================ */

/**
 * @brief Get current jog wheel state
 * 
 * @return Pointer to state structure (read-only)
 */
const jog_state_t* jog_wheel_get_state(void);

/**
 * @brief Check if wheel is currently touched
 * 
 * @return true if touched
 */
bool jog_wheel_is_touched(void);

/**
 * @brief Get current action
 * 
 * @return Current action type
 */
jog_action_t jog_wheel_get_action(void);

/**
 * @brief Get current velocity (normalized)
 * 
 * @return Velocity from -1.0 (max CCW) to +1.0 (max CW)
 */
float jog_wheel_get_velocity(void);

/**
 * @brief Get scratch output value
 * 
 * Returns playback speed for scratch mode.
 * 
 * @return Scratch value (-2.0 reverse to +2.0 fast forward)
 */
float jog_wheel_get_scratch(void);

/**
 * @brief Get nudge output value
 * 
 * Returns tempo adjustment amount.
 * 
 * @return Nudge value (-1.0 to +1.0)
 */
float jog_wheel_get_nudge(void);

/**
 * @brief Get search output value
 * 
 * Returns seek amount per update.
 * 
 * @return Search value in seconds (positive = forward)
 */
float jog_wheel_get_search(void);

/**
 * @brief Get brake output value
 * 
 * Returns brake effect intensity.
 * 
 * @return Brake value (0.0 to 1.0)
 */
float jog_wheel_get_brake(void);

/* ============================================================================
 * Touch Calibration
 * ============================================================================ */

/**
 * @brief Start touch sensor calibration
 * 
 * Begins calibration process. Leave jog wheel untouched.
 * 
 * @return true if calibration started
 */
bool jog_wheel_calibration_start(void);

/**
 * @brief Capture baseline (no touch) reading
 */
void jog_wheel_calibration_baseline(void);

/**
 * @brief Capture touched reading
 */
void jog_wheel_calibration_touched(void);

/**
 * @brief Finish calibration and apply values
 * 
 * @return true if calibration is valid
 */
bool jog_wheel_calibration_finish(void);

/**
 * @brief Get touch calibration data
 * 
 * @param cal Output calibration structure
 */
void jog_wheel_get_touch_calibration(jog_touch_cal_t *cal);

/**
 * @brief Set touch calibration data
 * 
 * @param cal Calibration data to apply
 * @return true on success
 */
bool jog_wheel_set_touch_calibration(const jog_touch_cal_t *cal);

/* ============================================================================
 * Integration Helpers
 * ============================================================================ */

/**
 * @brief Enable/disable slip mode integration
 * 
 * When enabled, scratching automatically triggers slip mode.
 * 
 * @param enable Enable slip integration
 */
void jog_wheel_set_slip_integration(bool enable);

/**
 * @brief Check if slip mode should be triggered
 * 
 * Helper for transport control integration.
 * 
 * @return true if jog wheel wants to start/stop slip
 */
bool jog_wheel_slip_trigger(void);

/**
 * @brief Get playback speed for scratch mode
 * 
 * Convenience function for audio engine integration.
 * Returns 1.0 when not scratching.
 * 
 * @return Playback speed multiplier
 */
float jog_wheel_get_playback_speed(void);

/**
 * @brief Get tempo adjustment for nudge mode
 * 
 * Convenience function for tempo control integration.
 * Returns 0.0 when not nudging.
 * 
 * @return Tempo adjustment in percent
 */
float jog_wheel_get_tempo_adjust(void);

/**
 * @brief Reset jog wheel state
 * 
 * Resets position, velocity, and action state.
 */
void jog_wheel_reset(void);

/* ============================================================================
 * Debug & Statistics
 * ============================================================================ */

/**
 * @brief Get raw touch sensor value
 * 
 * For debugging and calibration UI.
 * 
 * @return Raw capacitive sensor reading
 */
uint16_t jog_wheel_get_raw_touch(void);

/**
 * @brief Get raw encoder position
 * 
 * @return Cumulative encoder position
 */
int32_t jog_wheel_get_raw_position(void);

/**
 * @brief Get peak velocity recorded
 * 
 * @return Peak velocity value
 */
float jog_wheel_get_peak_velocity(void);

/**
 * @brief Reset statistics
 */
void jog_wheel_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* JOG_WHEEL_H */
