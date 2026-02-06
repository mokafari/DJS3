/**
 * @file dsp_pipeline.h
 * @brief DSP effect pipeline for real-time audio processing
 * 
 * Provides a chainable effect pipeline with:
 * - Filter (low-pass, high-pass, band-pass)
 * - 3-band EQ (low, mid, high)
 * - Echo/Delay effect
 * - Master limiter (prevents clipping)
 * 
 * Thread-safe parameter updates for real-time control.
 */

#ifndef DSP_PIPELINE_H
#define DSP_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of effects in the pipeline
#define DSP_MAX_EFFECTS 8

// Echo buffer size (max delay ~500ms at 44.1kHz stereo)
#define DSP_ECHO_BUFFER_SAMPLES (44100 / 2)

/**
 * @brief DSP effect types
 */
typedef enum {
    DSP_EFFECT_NONE = 0,
    DSP_EFFECT_FILTER,       ///< Low-pass, high-pass, or band-pass filter
    DSP_EFFECT_EQ,           ///< 3-band parametric EQ
    DSP_EFFECT_ECHO,         ///< Delay/echo effect
    DSP_EFFECT_LIMITER,      ///< Brick-wall limiter
    DSP_EFFECT_MAX
} dsp_effect_t;

/**
 * @brief Filter modes
 */
typedef enum {
    DSP_FILTER_LOWPASS = 0,
    DSP_FILTER_HIGHPASS,
    DSP_FILTER_BANDPASS
} dsp_filter_mode_t;

/**
 * @brief Filter parameters
 */
typedef struct {
    dsp_filter_mode_t mode;  ///< Filter type
    float cutoff_hz;         ///< Cutoff frequency (20-20000 Hz)
    float resonance;         ///< Resonance/Q (0.5-10.0)
} dsp_filter_params_t;

/**
 * @brief 3-band EQ parameters
 */
typedef struct {
    float low_gain;          ///< Low band gain (0.0-2.0, 1.0 = unity)
    float mid_gain;          ///< Mid band gain (0.0-2.0, 1.0 = unity)
    float high_gain;         ///< High band gain (0.0-2.0, 1.0 = unity)
    float low_freq;          ///< Low crossover frequency (default 200 Hz)
    float high_freq;         ///< High crossover frequency (default 3000 Hz)
} dsp_eq_params_t;

/**
 * @brief Echo/delay parameters
 */
typedef struct {
    float delay_ms;          ///< Delay time in milliseconds (1-500)
    float feedback;          ///< Feedback amount (0.0-0.95)
    float wet_mix;           ///< Wet/dry mix (0.0-1.0, 0=dry, 1=full wet)
} dsp_echo_params_t;

/**
 * @brief Limiter parameters
 */
typedef struct {
    float threshold;         ///< Threshold level (0.0-1.0)
    float release_ms;        ///< Release time in ms
    float ceiling;           ///< Output ceiling (0.0-1.0)
} dsp_limiter_params_t;

/**
 * @brief Effect configuration (union of all effect parameter types)
 */
typedef struct {
    dsp_effect_t type;       ///< Effect type
    bool bypass;             ///< Bypass flag (true = effect skipped)
    union {
        dsp_filter_params_t filter;
        dsp_eq_params_t eq;
        dsp_echo_params_t echo;
        dsp_limiter_params_t limiter;
    } params;
} dsp_effect_config_t;

/**
 * @brief Filter internal state (biquad)
 */
typedef struct {
    float b0, b1, b2;        ///< Feedforward coefficients
    float a1, a2;            ///< Feedback coefficients
    float x1_l, x2_l;        ///< Input history (left)
    float y1_l, y2_l;        ///< Output history (left)
    float x1_r, x2_r;        ///< Input history (right)
    float y1_r, y2_r;        ///< Output history (right)
} dsp_filter_state_t;

/**
 * @brief EQ internal state (3 biquad filters)
 */
typedef struct {
    dsp_filter_state_t low;
    dsp_filter_state_t mid;
    dsp_filter_state_t high;
} dsp_eq_state_t;

/**
 * @brief Echo internal state
 */
typedef struct {
    int16_t *buffer_l;       ///< Left channel delay buffer
    int16_t *buffer_r;       ///< Right channel delay buffer
    size_t buffer_size;      ///< Buffer size in samples
    size_t write_pos;        ///< Current write position
} dsp_echo_state_t;

/**
 * @brief Limiter internal state
 */
typedef struct {
    float envelope;          ///< Current envelope level
    float gain_reduction;    ///< Current gain reduction
} dsp_limiter_state_t;

/**
 * @brief Effect slot (config + state)
 */
typedef struct {
    dsp_effect_config_t config;
    union {
        dsp_filter_state_t filter;
        dsp_eq_state_t eq;
        dsp_echo_state_t echo;
        dsp_limiter_state_t limiter;
    } state;
    bool active;             ///< Slot is in use
} dsp_effect_slot_t;

/**
 * @brief Master limiter configuration
 */
typedef struct {
    bool enabled;
    float threshold;         ///< Threshold (0.0-1.0, default 0.95)
    float ceiling;           ///< Output ceiling (0.0-1.0, default 0.99)
    float release_ms;        ///< Release time (default 50ms)
} dsp_master_limiter_t;

/**
 * @brief DSP pipeline structure
 */
typedef struct {
    dsp_effect_slot_t effects[DSP_MAX_EFFECTS];
    size_t effect_count;
    dsp_master_limiter_t master_limiter;
    dsp_limiter_state_t master_limiter_state;
    SemaphoreHandle_t mutex;
    uint32_t sample_rate;
    bool initialized;
} dsp_pipeline_t;

// ============================================================================
// Pipeline Lifecycle
// ============================================================================

/**
 * @brief Initialize DSP pipeline
 * 
 * @param pipeline Pointer to pipeline structure
 * @param sample_rate Audio sample rate (typically 44100)
 * @return true on success, false on failure
 */
bool dsp_pipeline_init(dsp_pipeline_t *pipeline, uint32_t sample_rate);

/**
 * @brief Destroy DSP pipeline and free resources
 * 
 * @param pipeline Pointer to pipeline structure
 */
void dsp_pipeline_destroy(dsp_pipeline_t *pipeline);

/**
 * @brief Reset all effect states (clear delay buffers, filter history)
 * 
 * @param pipeline Pointer to pipeline structure
 */
void dsp_pipeline_reset(dsp_pipeline_t *pipeline);

// ============================================================================
// Effect Management
// ============================================================================

/**
 * @brief Add an effect to the pipeline
 * 
 * @param pipeline Pointer to pipeline structure
 * @param config Effect configuration
 * @return Effect slot index (0-7) or -1 on failure
 */
int dsp_pipeline_add_effect(dsp_pipeline_t *pipeline, const dsp_effect_config_t *config);

/**
 * @brief Remove an effect from the pipeline
 * 
 * @param pipeline Pointer to pipeline structure
 * @param slot_index Effect slot to remove
 * @return true on success, false on failure
 */
bool dsp_pipeline_remove_effect(dsp_pipeline_t *pipeline, int slot_index);

/**
 * @brief Update effect parameters (thread-safe)
 * 
 * @param pipeline Pointer to pipeline structure
 * @param slot_index Effect slot to update
 * @param config New effect configuration
 * @return true on success, false on failure
 */
bool dsp_pipeline_update_effect(dsp_pipeline_t *pipeline, int slot_index, 
                                 const dsp_effect_config_t *config);

/**
 * @brief Set effect bypass state
 * 
 * @param pipeline Pointer to pipeline structure
 * @param slot_index Effect slot
 * @param bypass true to bypass, false to enable
 * @return true on success, false on failure
 */
bool dsp_pipeline_set_bypass(dsp_pipeline_t *pipeline, int slot_index, bool bypass);

/**
 * @brief Get effect configuration
 * 
 * @param pipeline Pointer to pipeline structure
 * @param slot_index Effect slot
 * @param config Output configuration
 * @return true on success, false on failure
 */
bool dsp_pipeline_get_effect(dsp_pipeline_t *pipeline, int slot_index,
                              dsp_effect_config_t *config);

// ============================================================================
// Master Limiter
// ============================================================================

/**
 * @brief Configure master limiter
 * 
 * @param pipeline Pointer to pipeline structure
 * @param config Limiter configuration
 */
void dsp_pipeline_set_master_limiter(dsp_pipeline_t *pipeline, 
                                      const dsp_master_limiter_t *config);

/**
 * @brief Enable/disable master limiter
 * 
 * @param pipeline Pointer to pipeline structure
 * @param enabled true to enable, false to bypass
 */
void dsp_pipeline_enable_master_limiter(dsp_pipeline_t *pipeline, bool enabled);

// ============================================================================
// Audio Processing
// ============================================================================

/**
 * @brief Process audio through the entire effect chain
 * 
 * Processing order: effect[0] → effect[1] → ... → master limiter
 * 
 * @param pipeline Pointer to pipeline structure
 * @param samples Interleaved stereo int16 samples (modified in-place)
 * @param num_frames Number of stereo frames to process
 */
void dsp_pipeline_process(dsp_pipeline_t *pipeline, int16_t *samples, size_t num_frames);

/**
 * @brief Process audio (float version for internal use)
 * 
 * @param pipeline Pointer to pipeline structure
 * @param samples_l Left channel float samples
 * @param samples_r Right channel float samples
 * @param num_frames Number of frames to process
 */
void dsp_pipeline_process_float(dsp_pipeline_t *pipeline, 
                                 float *samples_l, float *samples_r, 
                                 size_t num_frames);

// ============================================================================
// Preset Configurations
// ============================================================================

/**
 * @brief Get default filter configuration
 * 
 * @param config Output configuration
 * @param mode Filter mode
 * @param cutoff_hz Cutoff frequency
 */
void dsp_filter_default(dsp_effect_config_t *config, dsp_filter_mode_t mode, float cutoff_hz);

/**
 * @brief Get default EQ configuration (flat response)
 * 
 * @param config Output configuration
 */
void dsp_eq_default(dsp_effect_config_t *config);

/**
 * @brief Get default echo configuration
 * 
 * @param config Output configuration
 * @param delay_ms Delay time in milliseconds
 */
void dsp_echo_default(dsp_effect_config_t *config, float delay_ms);

/**
 * @brief Get default limiter configuration
 * 
 * @param config Output configuration
 */
void dsp_limiter_default(dsp_effect_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // DSP_PIPELINE_H
