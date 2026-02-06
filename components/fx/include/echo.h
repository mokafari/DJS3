/**
 * @file echo.h
 * @brief Tempo-synced echo/delay effect for DJS3
 *
 * Provides a stereo delay effect with tempo-synchronized delay times,
 * feedback control, and wet/dry mixing. Optimized for ESP32 with PSRAM.
 */

#ifndef FX_ECHO_H
#define FX_ECHO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default sample rate
 */
#define ECHO_DEFAULT_SAMPLE_RATE 44100

/**
 * @brief Maximum feedback to prevent runaway (95%)
 */
#define ECHO_MAX_FEEDBACK 0.95f

/**
 * @brief Beat division presets
 */
typedef enum {
    ECHO_BEAT_1_4 = 0,  /**< 1/4 beat delay */
    ECHO_BEAT_1_2,      /**< 1/2 beat delay */
    ECHO_BEAT_3_4,      /**< 3/4 beat delay */
    ECHO_BEAT_1,        /**< Full beat delay */
    ECHO_BEAT_COUNT
} echo_beat_division_t;

/**
 * @brief Echo effect state structure
 * 
 * Buffer is dynamically allocated in PSRAM when available.
 */
typedef struct {
    float sample_rate;              /**< Audio sample rate in Hz */
    float bpm;                      /**< Current tempo in beats per minute */
    echo_beat_division_t division;  /**< Current beat division preset */
    float feedback;                 /**< Feedback amount (0.0 - 0.95) */
    float mix;                      /**< Wet/dry mix (0.0 = dry, 1.0 = wet) */
    
    size_t delay_samples;           /**< Current delay in samples (stereo pairs * 2) */
    size_t write_pos;               /**< Write position in circular buffer */
    size_t buffer_size;             /**< Total buffer size in samples */
    
    int16_t *buffer;                /**< Circular delay buffer (stereo interleaved) */
    bool initialized;               /**< Initialization flag */
} echo_t;

/**
 * @brief Initialize echo effect with dynamically allocated delay buffer
 * 
 * Allocates delay buffer in PSRAM if available, otherwise in regular heap.
 * Buffer is sized for maximum delay of 1 beat at 60 BPM.
 * 
 * @param e Pointer to echo state structure
 * @return true on success, false on allocation failure
 */
bool echo_init(echo_t *e);

/**
 * @brief Free echo resources
 * 
 * @param e Pointer to echo state structure
 */
void echo_deinit(echo_t *e);

/**
 * @brief Set tempo for delay time calculation
 * 
 * @param e Pointer to echo state structure
 * @param bpm Tempo in beats per minute (30.0 - 300.0)
 */
void echo_set_bpm(echo_t *e, float bpm);

/**
 * @brief Set delay time as beat division
 * 
 * @param e Pointer to echo state structure
 * @param division Beat division (1/4, 1/2, 3/4, or 1 beat)
 */
void echo_set_beat_division(echo_t *e, echo_beat_division_t division);

/**
 * @brief Set feedback amount
 * 
 * Higher feedback creates more repeating echoes. Clamped to 95% maximum
 * to prevent runaway feedback.
 * 
 * @param e Pointer to echo state structure
 * @param feedback Feedback level (0.0 - 1.0, will be clamped to 0.95 max)
 */
void echo_set_feedback(echo_t *e, float feedback);

/**
 * @brief Set wet/dry mix
 * 
 * @param e Pointer to echo state structure
 * @param mix Mix level (0.0 = fully dry/original, 1.0 = fully wet/delayed)
 */
void echo_set_mix(echo_t *e, float mix);

/**
 * @brief Process audio through echo effect (in-place)
 * 
 * Processes stereo interleaved 16-bit PCM audio in-place.
 * 
 * @param e Pointer to echo state structure
 * @param samples Pointer to stereo interleaved audio buffer
 * @param num_frames Number of stereo frames (sample pairs) to process
 */
void echo_process(echo_t *e, int16_t *samples, size_t num_frames);

/**
 * @brief Clear delay buffer
 * 
 * Clears all delayed audio from the buffer. Use when:
 * - Switching tracks
 * - Stopping playback
 * - Resetting the effect
 * 
 * @param e Pointer to echo state structure
 */
void echo_clear(echo_t *e);

/**
 * @brief Get current delay time in milliseconds
 * 
 * @param e Pointer to echo state structure
 * @return Delay time in milliseconds
 */
float echo_get_delay_ms(const echo_t *e);

/**
 * @brief Get beat division ratio
 * 
 * @param division Beat division enum value
 * @return Ratio as float (0.25, 0.5, 0.75, or 1.0)
 */
float echo_division_to_ratio(echo_beat_division_t division);

#ifdef __cplusplus
}
#endif

#endif /* FX_ECHO_H */
