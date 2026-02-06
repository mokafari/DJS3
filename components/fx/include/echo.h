/**
 * @file echo.h
 * @brief Tempo-synced echo/delay effect for DJS3
 *
 * Provides a stereo delay effect with tempo-synchronized delay times,
 * feedback control, and wet/dry mixing.
 */

#ifndef FX_ECHO_H
#define FX_ECHO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum delay buffer size in stereo samples
 * 
 * Sized for 1 beat at 60 BPM @ 48kHz stereo = 48000 * 2 = 96000 samples
 * Adding headroom for slower tempos
 */
#define ECHO_MAX_DELAY_SAMPLES (96000 * 2)

/**
 * @brief Echo effect state structure
 */
typedef struct {
    float sample_rate;          /**< Audio sample rate in Hz */
    float bpm;                  /**< Current tempo in beats per minute */
    float delay_ratio;          /**< Delay time as fraction of beat (0.25, 0.5, 0.75, 1.0) */
    float feedback;             /**< Feedback amount (0.0 - 0.95) */
    float mix;                  /**< Wet/dry mix (0.0 = dry, 1.0 = wet) */
    
    size_t delay_samples;       /**< Current delay in stereo sample pairs */
    size_t write_pos;           /**< Write position in circular buffer */
    
    int16_t buffer[ECHO_MAX_DELAY_SAMPLES];  /**< Circular delay buffer (stereo interleaved) */
} echo_t;

/**
 * @brief Initialize echo effect
 * 
 * @param e Pointer to echo state structure
 * @param sample_rate Audio sample rate in Hz
 */
void echo_init(echo_t *e, float sample_rate);

/**
 * @brief Set tempo for delay time calculation
 * 
 * @param e Pointer to echo state structure
 * @param bpm Tempo in beats per minute (30.0 - 300.0)
 */
void echo_set_bpm(echo_t *e, float bpm);

/**
 * @brief Set delay time as fraction of beat
 * 
 * @param e Pointer to echo state structure
 * @param ratio Delay ratio (0.25 = 1/4 beat, 0.5 = 1/2 beat, 0.75 = 3/4 beat, 1.0 = 1 beat)
 */
void echo_set_delay_ratio(echo_t *e, float ratio);

/**
 * @brief Set feedback amount
 * 
 * @param e Pointer to echo state structure
 * @param feedback Feedback level (0.0 - 0.95, clamped to prevent runaway)
 */
void echo_set_feedback(echo_t *e, float feedback);

/**
 * @brief Set wet/dry mix
 * 
 * @param e Pointer to echo state structure
 * @param wet_dry Mix level (0.0 = fully dry, 1.0 = fully wet)
 */
void echo_set_mix(echo_t *e, float wet_dry);

/**
 * @brief Process audio through echo effect
 * 
 * Processes stereo interleaved 16-bit PCM audio in-place.
 * 
 * @param e Pointer to echo state structure
 * @param samples Pointer to stereo interleaved audio buffer
 * @param num_samples Number of stereo sample pairs to process
 */
void echo_process(echo_t *e, int16_t *samples, size_t num_samples);

#ifdef __cplusplus
}
#endif

#endif /* FX_ECHO_H */
