/**
 * @file eq.h
 * @brief 3-Band EQ with kill switches for DJ applications
 * 
 * Provides a proper 3-band equalizer with:
 * - Low shelf filter at 250Hz
 * - Mid peaking filter (250Hz - 4kHz range)
 * - High shelf filter at 4kHz
 * - Kill switch for each band (complete mute)
 * - Biquad filter implementation for accurate frequency separation
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
 * @brief Biquad filter state for a single channel
 */
typedef struct {
    float b0, b1, b2;  // Numerator coefficients
    float a1, a2;      // Denominator coefficients (a0 normalized to 1)
    float z1, z2;      // Delay line state
} eq_biquad_t;

/**
 * @brief 3-Band EQ state structure
 * 
 * Contains biquad filters for low/mid/high bands with stereo processing.
 * Each band has independent gain control and kill switch.
 */
typedef struct {
    // Low band (shelf at 250Hz)
    eq_biquad_t low_l;
    eq_biquad_t low_r;
    float gain_low;      // Linear gain (0.0 - 2.0, 1.0 = unity)
    bool kill_low;       // Kill switch (mutes entire low band)
    
    // Mid band (peaking at 1kHz)
    eq_biquad_t mid_l;
    eq_biquad_t mid_r;
    float gain_mid;      // Linear gain
    bool kill_mid;       // Kill switch
    
    // High band (shelf at 4kHz)
    eq_biquad_t high_l;
    eq_biquad_t high_r;
    float gain_high;     // Linear gain
    bool kill_high;      // Kill switch
    
    // Crossover filters (Linkwitz-Riley for band separation)
    eq_biquad_t xover_low_lp_l, xover_low_lp_r;   // Lowpass at 250Hz
    eq_biquad_t xover_low_hp_l, xover_low_hp_r;   // Highpass at 250Hz
    eq_biquad_t xover_high_lp_l, xover_high_lp_r; // Lowpass at 4kHz
    eq_biquad_t xover_high_hp_l, xover_high_hp_r; // Highpass at 4kHz
    
    uint32_t sample_rate;
    bool enabled;
} eq_3band_t;

/**
 * @brief Initialize 3-band EQ with default settings
 * 
 * Sets up crossover filters at 250Hz and 4kHz with flat response.
 * All bands set to unity gain, no kills active.
 * 
 * @param eq Pointer to EQ state structure
 * @param sample_rate Audio sample rate (e.g., 44100)
 */
void eq_3band_init(eq_3band_t *eq, uint32_t sample_rate);

/**
 * @brief Set gain for low band
 * 
 * @param eq Pointer to EQ state
 * @param gain_db Gain in dB (-24 to +12)
 */
void eq_3band_set_low_gain(eq_3band_t *eq, float gain_db);

/**
 * @brief Set gain for mid band
 * 
 * @param eq Pointer to EQ state
 * @param gain_db Gain in dB (-24 to +12)
 */
void eq_3band_set_mid_gain(eq_3band_t *eq, float gain_db);

/**
 * @brief Set gain for high band
 * 
 * @param eq Pointer to EQ state
 * @param gain_db Gain in dB (-24 to +12)
 */
void eq_3band_set_high_gain(eq_3band_t *eq, float gain_db);

/**
 * @brief Set all band gains at once
 * 
 * @param eq Pointer to EQ state
 * @param low_db Low band gain in dB
 * @param mid_db Mid band gain in dB
 * @param high_db High band gain in dB
 */
void eq_3band_set_gains(eq_3band_t *eq, float low_db, float mid_db, float high_db);

/**
 * @brief Set kill switch for low band
 * 
 * @param eq Pointer to EQ state
 * @param kill True to kill (mute) low frequencies
 */
void eq_3band_set_low_kill(eq_3band_t *eq, bool kill);

/**
 * @brief Set kill switch for mid band
 * 
 * @param eq Pointer to EQ state
 * @param kill True to kill (mute) mid frequencies
 */
void eq_3band_set_mid_kill(eq_3band_t *eq, bool kill);

/**
 * @brief Set kill switch for high band
 * 
 * @param eq Pointer to EQ state
 * @param kill True to kill (mute) high frequencies
 */
void eq_3band_set_high_kill(eq_3band_t *eq, bool kill);

/**
 * @brief Toggle kill switch for low band
 * 
 * @param eq Pointer to EQ state
 * @return New kill state
 */
bool eq_3band_toggle_low_kill(eq_3band_t *eq);

/**
 * @brief Toggle kill switch for mid band
 * 
 * @param eq Pointer to EQ state
 * @return New kill state
 */
bool eq_3band_toggle_mid_kill(eq_3band_t *eq);

/**
 * @brief Toggle kill switch for high band
 * 
 * @param eq Pointer to EQ state
 * @return New kill state
 */
bool eq_3band_toggle_high_kill(eq_3band_t *eq);

/**
 * @brief Enable or disable EQ processing
 * 
 * @param eq Pointer to EQ state
 * @param enabled True to enable, false to bypass
 */
void eq_3band_set_enabled(eq_3band_t *eq, bool enabled);

/**
 * @brief Process audio through 3-band EQ (IRAM optimized)
 * 
 * Splits audio into low/mid/high bands using crossover filters,
 * applies gain to each band, recombines, and applies soft limiting.
 * 
 * @param eq      Pointer to EQ state
 * @param buffer  Stereo interleaved int16 buffer (modified in-place)
 * @param samples Number of stereo frames to process
 * 
 * @note Thread Safety: Not thread-safe. Call from single audio task only.
 * @note Maximum 256 samples per call due to static scratch buffer.
 */
void eq_3band_process(eq_3band_t *eq, int16_t *buffer, size_t samples);

/**
 * @brief Reset EQ filter state (clears delay lines)
 * 
 * Call when switching tracks or seeking to prevent filter transients.
 * 
 * @param eq Pointer to EQ state
 */
void eq_3band_reset(eq_3band_t *eq);

/**
 * @brief Recalculate crossover filter coefficients
 * 
 * Call this if sample rate changes. Normally called automatically by init.
 * 
 * @param eq Pointer to EQ state
 */
void eq_3band_recalculate_coeffs(eq_3band_t *eq);

#ifdef __cplusplus
}
#endif

#endif // EQ_H
