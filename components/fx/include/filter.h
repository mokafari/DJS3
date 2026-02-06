/**
 * @file filter.h
 * @brief DJ-style filters: 3-band EQ/Isolator and Resonant HPF/LPF
 * 
 * Provides:
 * - Classic DJ mixer-style 3-band EQ with soft limiting
 * - Resonant high-pass and low-pass filters (20Hz-20kHz)
 * - Biquad IIR filter design for CPU-efficient processing
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

/* ============================================================================
 * Resonant Filter (HPF/LPF)
 * ============================================================================ */

/**
 * @brief Filter type selection
 */
typedef enum {
    FILTER_TYPE_LOWPASS = 0,    /**< Resonant low-pass filter */
    FILTER_TYPE_HIGHPASS        /**< Resonant high-pass filter */
} filter_type_t;

/**
 * @brief Resonant filter state
 * 
 * Biquad IIR filter with transposed direct form II implementation.
 * Supports stereo processing with independent state per channel.
 */
typedef struct {
    // Biquad coefficients (normalized)
    float b0, b1, b2;   /**< Feedforward coefficients */
    float a1, a2;       /**< Feedback coefficients (a0 normalized to 1.0) */
    
    // Filter state (transposed direct form II)
    float w1_l, w2_l;   /**< Left channel delay elements */
    float w1_r, w2_r;   /**< Right channel delay elements */
    
    // Parameters
    filter_type_t type;     /**< Filter type (LPF/HPF) */
    float cutoff_hz;        /**< Cutoff frequency in Hz (20-20000) */
    float resonance;        /**< Resonance/Q factor (0.5-20.0) */
    uint32_t sample_rate;   /**< Audio sample rate */
    bool enabled;           /**< Processing enabled flag */
} resonant_filter_t;

/**
 * @brief Initialize resonant filter
 * 
 * @param filter Pointer to filter state structure
 * @param sample_rate Audio sample rate (e.g., 44100)
 * @param type Filter type (FILTER_TYPE_LOWPASS or FILTER_TYPE_HIGHPASS)
 */
void resonant_filter_init(resonant_filter_t *filter, uint32_t sample_rate, filter_type_t type);

/**
 * @brief Set filter cutoff frequency
 * 
 * @param filter Pointer to filter state
 * @param cutoff_hz Cutoff frequency in Hz (clamped to 20-20000)
 */
void resonant_filter_set_cutoff(resonant_filter_t *filter, float cutoff_hz);

/**
 * @brief Set filter resonance (Q factor)
 * 
 * Higher values create a peak at the cutoff frequency.
 * Values around 0.707 give Butterworth response (no peak).
 * Values above 10 create strong resonance/self-oscillation character.
 * 
 * @param filter Pointer to filter state
 * @param resonance Q factor (clamped to 0.5-20.0)
 */
void resonant_filter_set_resonance(resonant_filter_t *filter, float resonance);

/**
 * @brief Set filter type (LPF/HPF)
 * 
 * @param filter Pointer to filter state
 * @param type Filter type
 */
void resonant_filter_set_type(resonant_filter_t *filter, filter_type_t type);

/**
 * @brief Enable or disable filter processing
 * 
 * @param filter Pointer to filter state
 * @param enabled True to enable, false to bypass
 */
void resonant_filter_set_enabled(resonant_filter_t *filter, bool enabled);

/**
 * @brief Process audio through resonant filter (IRAM optimized)
 * 
 * Applies biquad IIR filter to stereo audio in-place.
 * Uses transposed direct form II for numerical stability.
 * 
 * @param filter Pointer to filter state
 * @param buffer Stereo interleaved int16 buffer (modified in-place)
 * @param samples Number of stereo frames to process
 * 
 * @note Thread Safety: Not thread-safe. Call from single audio task only.
 */
void resonant_filter_process(resonant_filter_t *filter, int16_t *buffer, size_t samples);

/**
 * @brief Reset filter state (clears delay lines)
 * 
 * Call when switching tracks or seeking to prevent filter transients.
 * 
 * @param filter Pointer to filter state
 */
void resonant_filter_reset(resonant_filter_t *filter);

/* ============================================================================
 * DJ EQ (3-Band Isolator)
 * ============================================================================ */

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
