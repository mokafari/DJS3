/**
 * @file eq.h
 * @brief 3-band parametric EQ with kill switches for DJ applications
 * 
 * Implements a classic 3-band DJ-style equalizer with:
 * - Low shelf filter at 250Hz
 * - Peaking mid filter at 2.5kHz
 * - High shelf filter at 8kHz
 * - Per-band gain control (+/- 12dB)
 * - Per-band kill switch (instant full cut)
 * - Biquad filters for each band
 */

#ifndef EQ_H
#define EQ_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default crossover frequencies (Hz)
 */
#define EQ_FREQ_LOW_DEFAULT    250.0f
#define EQ_FREQ_MID_DEFAULT    2500.0f
#define EQ_FREQ_HIGH_DEFAULT   8000.0f

/**
 * @brief Gain range in dB
 */
#define EQ_GAIN_MIN_DB         -12.0f
#define EQ_GAIN_MAX_DB         12.0f

/**
 * @brief Q factors for each band
 */
#define EQ_Q_SHELF             0.707f   // Butterworth-style shelving
#define EQ_Q_PEAKING           1.0f     // Moderate bandwidth for mid

/**
 * @brief Biquad filter state for a single channel
 */
typedef struct {
    float z1;   // z^-1 delay
    float z2;   // z^-2 delay
} eq_biquad_state_t;

/**
 * @brief Biquad filter coefficients (Direct Form II Transposed)
 */
typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} eq_biquad_coeffs_t;

/**
 * @brief Single EQ band state
 */
typedef struct {
    eq_biquad_coeffs_t coeffs;
    eq_biquad_state_t state_l;  // Left channel state
    eq_biquad_state_t state_r;  // Right channel state
    float gain_db;              // Current gain in dB
    float freq_hz;              // Center/corner frequency
    float q;                    // Q factor
    bool kill;                  // Kill switch (instant mute)
} eq_band_t;

/**
 * @brief 3-band EQ state
 */
typedef struct {
    eq_band_t low;              // Low shelf band
    eq_band_t mid;              // Peaking mid band
    eq_band_t high;             // High shelf band
    uint32_t sample_rate;
    bool enabled;
} eq_state_t;

/**
 * @brief Initialize 3-band EQ with default settings
 * 
 * Sets up filters with:
 * - Low shelf at 250Hz, 0dB gain
 * - Mid peak at 2.5kHz, 0dB gain
 * - High shelf at 8kHz, 0dB gain
 * 
 * @param eq Pointer to EQ state structure
 * @param sample_rate Audio sample rate (e.g., 44100)
 */
void eq_init(eq_state_t *eq, uint32_t sample_rate);

/**
 * @brief Process stereo audio through the 3-band EQ
 * 
 * @param eq Pointer to EQ state
 * @param buffer Stereo interleaved int16 buffer (modified in-place)
 * @param num_frames Number of stereo frames to process
 * 
 * @note Thread Safety: Not thread-safe. Call from single audio task only.
 */
void eq_process(eq_state_t *eq, int16_t *buffer, size_t num_frames);

/**
 * @brief Set low band gain
 * 
 * @param eq Pointer to EQ state
 * @param gain_db Gain in dB (-12 to +12)
 */
void eq_set_low_gain(eq_state_t *eq, float gain_db);

/**
 * @brief Set mid band gain
 * 
 * @param eq Pointer to EQ state
 * @param gain_db Gain in dB (-12 to +12)
 */
void eq_set_mid_gain(eq_state_t *eq, float gain_db);

/**
 * @brief Set high band gain
 * 
 * @param eq Pointer to EQ state
 * @param gain_db Gain in dB (-12 to +12)
 */
void eq_set_high_gain(eq_state_t *eq, float gain_db);

/**
 * @brief Set low band kill switch
 * 
 * @param eq Pointer to EQ state
 * @param kill True to kill (mute) the band
 */
void eq_set_low_kill(eq_state_t *eq, bool kill);

/**
 * @brief Set mid band kill switch
 * 
 * @param eq Pointer to EQ state
 * @param kill True to kill (mute) the band
 */
void eq_set_mid_kill(eq_state_t *eq, bool kill);

/**
 * @brief Set high band kill switch
 * 
 * @param eq Pointer to EQ state
 * @param kill True to kill (mute) the band
 */
void eq_set_high_kill(eq_state_t *eq, bool kill);

/**
 * @brief Enable or disable EQ processing
 * 
 * @param eq Pointer to EQ state
 * @param enabled True to enable, false to bypass
 */
void eq_set_enabled(eq_state_t *eq, bool enabled);

/**
 * @brief Reset EQ filter states (clears delay lines)
 * 
 * Call when switching tracks or seeking to prevent filter transients.
 * 
 * @param eq Pointer to EQ state
 */
void eq_reset(eq_state_t *eq);

/**
 * @brief Set custom frequency for a band
 * 
 * @param eq Pointer to EQ state
 * @param band 0=low, 1=mid, 2=high
 * @param freq_hz New frequency in Hz
 */
void eq_set_band_freq(eq_state_t *eq, int band, float freq_hz);

#ifdef __cplusplus
}
#endif

#endif // EQ_H
