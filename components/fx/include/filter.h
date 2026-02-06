/**
 * @file filter.h
 * @brief Resonant biquad filter with high-pass and low-pass modes
 * 
 * Implements a DJ-style resonant filter with:
 * - Switchable low-pass / high-pass modes
 * - Cutoff frequency: 20Hz - 20kHz
 * - Resonance (Q factor): 0.5 - 20.0
 * - Direct Form II Transposed biquad for stability
 * - Optimized for ESP32 float operations
 */

#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Filter mode (low-pass or high-pass)
 */
typedef enum {
    FILTER_MODE_LOWPASS = 0,    /**< Low-pass filter - cuts highs */
    FILTER_MODE_HIGHPASS = 1    /**< High-pass filter - cuts lows */
} filter_mode_t;

/**
 * @brief Biquad filter state for one channel
 * 
 * Uses Direct Form II Transposed for numerical stability:
 *   y[n] = b0*x[n] + z1
 *   z1   = b1*x[n] - a1*y[n] + z2
 *   z2   = b2*x[n] - a2*y[n]
 */
typedef struct {
    float z1;   /**< Delay element 1 */
    float z2;   /**< Delay element 2 */
} biquad_state_t;

/**
 * @brief Resonant filter instance
 */
typedef struct {
    // Biquad coefficients (normalized: a0 = 1.0)
    float b0, b1, b2;   /**< Feedforward coefficients */
    float a1, a2;       /**< Feedback coefficients */
    
    // Per-channel state
    biquad_state_t state_l; /**< Left channel state */
    biquad_state_t state_r; /**< Right channel state */
    
    // Parameters
    float cutoff_hz;        /**< Cutoff frequency in Hz */
    float resonance;        /**< Resonance / Q factor */
    filter_mode_t mode;     /**< Filter mode (LP/HP) */
    uint32_t sample_rate;   /**< Sample rate in Hz */
    
    bool enabled;           /**< Enable/bypass flag */
    bool coeffs_dirty;      /**< Coefficients need recalculation */
} resonant_filter_t;

/**
 * @brief Initialize resonant filter
 * 
 * Sets up filter with default parameters:
 * - Mode: Low-pass
 * - Cutoff: 1000 Hz
 * - Resonance: 0.707 (Butterworth)
 * 
 * @param filter Pointer to filter instance
 * @param sample_rate Audio sample rate (e.g., 44100)
 */
void filter_init(resonant_filter_t *filter, uint32_t sample_rate);

/**
 * @brief Set filter cutoff frequency
 * 
 * @param filter Pointer to filter instance
 * @param cutoff_hz Cutoff frequency (20.0 - 20000.0 Hz)
 */
void filter_set_cutoff(resonant_filter_t *filter, float cutoff_hz);

/**
 * @brief Set filter resonance (Q factor)
 * 
 * Higher values create a resonant peak at the cutoff frequency.
 * Values above ~10 can cause self-oscillation.
 * 
 * @param filter Pointer to filter instance
 * @param resonance Q factor (0.5 - 20.0, 0.707 = Butterworth)
 */
void filter_set_resonance(resonant_filter_t *filter, float resonance);

/**
 * @brief Set filter mode (low-pass or high-pass)
 * 
 * @param filter Pointer to filter instance
 * @param mode FILTER_MODE_LOWPASS or FILTER_MODE_HIGHPASS
 */
void filter_set_mode(resonant_filter_t *filter, filter_mode_t mode);

/**
 * @brief Enable or disable filter processing
 * 
 * @param filter Pointer to filter instance
 * @param enabled True to enable, false to bypass
 */
void filter_set_enabled(resonant_filter_t *filter, bool enabled);

/**
 * @brief Process stereo audio through filter (in-place)
 * 
 * Processes stereo interleaved 16-bit PCM audio.
 * Uses IRAM for hot loop optimization on ESP32.
 * 
 * @param filter Pointer to filter instance
 * @param buffer Stereo interleaved int16 buffer (modified in-place)
 * @param num_frames Number of stereo frames to process
 * 
 * @note Thread Safety: Not thread-safe. Call from single audio task only.
 */
void filter_process(resonant_filter_t *filter, int16_t *buffer, size_t num_frames);

/**
 * @brief Reset filter state (clears delay lines)
 * 
 * Call when switching tracks or seeking to prevent transients.
 * 
 * @param filter Pointer to filter instance
 */
void filter_reset(resonant_filter_t *filter);

/**
 * @brief Get current cutoff frequency
 * 
 * @param filter Pointer to filter instance
 * @return Current cutoff in Hz
 */
float filter_get_cutoff(const resonant_filter_t *filter);

/**
 * @brief Get current resonance
 * 
 * @param filter Pointer to filter instance
 * @return Current Q factor
 */
float filter_get_resonance(const resonant_filter_t *filter);

/**
 * @brief Get current filter mode
 * 
 * @param filter Pointer to filter instance
 * @return Current mode (LP/HP)
 */
filter_mode_t filter_get_mode(const resonant_filter_t *filter);

#ifdef __cplusplus
}
#endif

#endif // FILTER_H
