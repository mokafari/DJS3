/**
 * @file filter.h
 * @brief DJ-style 3-band EQ/Isolator with soft limiting
 * 
 * Provides a classic DJ mixer-style 3-band EQ with:
 * - Low shelf filter at 200Hz
 * - Mid peaking filter at 1kHz  
 * - High shelf filter at 5kHz
 * - Polynomial soft limiter for tube-like saturation
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
 * @brief DJ EQ state (3-band isolator)
 * 
 * Contains biquad filter coefficients and delay lines for stereo processing.
 * Gains map from -1.0 (kill/mute) to 1.0 (boost).
 */
typedef struct {
    // Biquad coefficients [b0, b1, b2, a1, a2]
    float coeffs_low[5];
    float coeffs_mid[5];
    float coeffs_high[5];
    
    // Delay lines for L/R channels (2 samples each for biquad)
    float w_low_l[2], w_low_r[2];
    float w_mid_l[2], w_mid_r[2];
    float w_high_l[2], w_high_r[2];
    
    // Gain controls: -1.0 (kill) to 1.0 (boost)
    // -1.0 = mute, 0.0 = unity, 1.0 = +6dB boost
    float gain_low;
    float gain_mid;
    float gain_high;
    
    uint32_t sample_rate;
    bool enabled;
} dj_eq_t;

/**
 * @brief Initialize DJ EQ with default settings
 * 
 * @param eq Pointer to EQ state structure
 * @param sample_rate Audio sample rate (e.g., 44100)
 */
void dj_eq_init(dj_eq_t *eq, uint32_t sample_rate);

/**
 * @brief Set EQ band gains
 * 
 * @param eq Pointer to EQ state
 * @param low  Low band gain (-1.0 to 1.0)
 * @param mid  Mid band gain (-1.0 to 1.0)
 * @param high High band gain (-1.0 to 1.0)
 * 
 * Example:
 *     dj_eq_set_gains(&eq, -1.0f, 1.0f, 0.0f);  // Kill bass, boost mids
 */
void dj_eq_set_gains(dj_eq_t *eq, float low, float mid, float high);

/**
 * @brief Enable or disable EQ processing
 * 
 * @param eq Pointer to EQ state
 * @param enabled True to enable, false to bypass
 */
void dj_eq_set_enabled(dj_eq_t *eq, bool enabled);

/**
 * @brief Process audio through EQ and soft limiter (IRAM optimized)
 * 
 * Applies 3-band EQ and polynomial soft limiter to stereo audio.
 * Uses static scratch buffer to avoid stack allocation.
 * 
 * @param eq      Pointer to EQ state
 * @param buffer  Stereo interleaved int16 buffer (modified in-place)
 * @param samples Number of stereo frames to process
 * 
 * @note Thread Safety: Not thread-safe. Call from single audio task only.
 */
void dj_eq_process(dj_eq_t *eq, int16_t *buffer, size_t samples);

/**
 * @brief Reset EQ filter state (clears delay lines)
 * 
 * Call when switching tracks or seeking to prevent filter transients.
 * 
 * @param eq Pointer to EQ state
 */
void dj_eq_reset(dj_eq_t *eq);

#ifdef __cplusplus
}
#endif

#endif // FILTER_H
