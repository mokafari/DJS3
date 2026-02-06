/**
 * @file echo.h
 * @brief Tempo-synced echo/delay effect with feedback
 * 
 * Provides a DJ-style echo effect with:
 * - Tempo-synchronized delay times (1/4, 1/2, 3/4, 1 beat)
 * - Adjustable feedback for repeating echoes
 * - Wet/dry mix control
 * - Circular buffer delay line for efficient memory usage
 */

#ifndef ECHO_H
#define ECHO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum delay time in milliseconds (2 seconds)
 * 
 * Supports tempos as low as 60 BPM with 1 beat delay (1000ms)
 * or 30 BPM with 1 beat delay (2000ms)
 */
#define ECHO_MAX_DELAY_MS       2000

/**
 * @brief Default sample rate
 */
#define ECHO_DEFAULT_SAMPLE_RATE 44100

/**
 * @brief Tempo-synced delay time divisions
 */
typedef enum {
    ECHO_SYNC_1_4 = 0,   /**< 1/4 beat (quarter note) */
    ECHO_SYNC_1_2,       /**< 1/2 beat (half note) */
    ECHO_SYNC_3_4,       /**< 3/4 beat (dotted half) */
    ECHO_SYNC_1_1,       /**< 1 beat (whole note) */
    ECHO_SYNC_COUNT      /**< Number of sync options */
} echo_sync_t;

/**
 * @brief Echo effect state
 * 
 * Contains circular buffer and parameters for stereo echo processing.
 */
typedef struct {
    // Circular buffer for delay line (stereo interleaved)
    int16_t *buffer_l;           /**< Left channel delay buffer */
    int16_t *buffer_r;           /**< Right channel delay buffer */
    size_t buffer_size;          /**< Buffer size in samples */
    size_t write_pos;            /**< Current write position */
    
    // Delay parameters
    size_t delay_samples;        /**< Current delay time in samples */
    float feedback;              /**< Feedback amount (0.0 to 0.95) */
    float mix;                   /**< Wet/dry mix (0.0 = dry, 1.0 = wet) */
    
    // Tempo sync
    float bpm;                   /**< Current tempo in BPM */
    echo_sync_t sync_mode;       /**< Beat division for sync */
    
    uint32_t sample_rate;        /**< Audio sample rate */
    bool enabled;                /**< Effect enable/bypass */
} echo_t;

/**
 * @brief Initialize echo effect
 * 
 * Allocates delay buffer and sets default parameters.
 * 
 * @param echo Pointer to echo state structure
 * @param sample_rate Audio sample rate (e.g., 44100)
 * @return true on success, false if buffer allocation fails
 * 
 * @note Call echo_deinit() to free allocated memory.
 */
bool echo_init(echo_t *echo, uint32_t sample_rate);

/**
 * @brief Deinitialize echo effect
 * 
 * Frees delay buffer memory.
 * 
 * @param echo Pointer to echo state structure
 */
void echo_deinit(echo_t *echo);

/**
 * @brief Set echo tempo (BPM)
 * 
 * Updates delay time based on current sync mode and new tempo.
 * 
 * @param echo Pointer to echo state
 * @param bpm Tempo in beats per minute (30.0 to 300.0)
 */
void echo_set_bpm(echo_t *echo, float bpm);

/**
 * @brief Set tempo sync mode
 * 
 * Changes the beat division used for delay time calculation.
 * 
 * @param echo Pointer to echo state
 * @param sync Beat division (1/4, 1/2, 3/4, or 1 beat)
 */
void echo_set_sync(echo_t *echo, echo_sync_t sync);

/**
 * @brief Set feedback amount
 * 
 * Controls how much of the delayed signal is fed back into the delay line.
 * Higher values create longer-lasting echoes.
 * 
 * @param echo Pointer to echo state
 * @param feedback Feedback amount (0.0 to 0.95, clamped for stability)
 */
void echo_set_feedback(echo_t *echo, float feedback);

/**
 * @brief Set wet/dry mix
 * 
 * @param echo Pointer to echo state
 * @param mix Mix amount (0.0 = fully dry, 1.0 = fully wet)
 */
void echo_set_mix(echo_t *echo, float mix);

/**
 * @brief Enable or disable echo processing
 * 
 * @param echo Pointer to echo state
 * @param enabled True to enable, false to bypass
 */
void echo_set_enabled(echo_t *echo, bool enabled);

/**
 * @brief Set manual delay time (bypasses tempo sync)
 * 
 * @param echo Pointer to echo state
 * @param delay_ms Delay time in milliseconds (clamped to ECHO_MAX_DELAY_MS)
 */
void echo_set_delay_ms(echo_t *echo, float delay_ms);

/**
 * @brief Get current delay time in milliseconds
 * 
 * @param echo Pointer to echo state
 * @return Current delay time in ms
 */
float echo_get_delay_ms(const echo_t *echo);

/**
 * @brief Calculate delay time from BPM and sync mode
 * 
 * Utility function to compute delay time.
 * Formula: delay_ms = (60000 / bpm) * beat_fraction
 * 
 * @param bpm Tempo in beats per minute
 * @param sync Beat division
 * @return Delay time in milliseconds
 */
float echo_calc_delay_ms(float bpm, echo_sync_t sync);

/**
 * @brief Process audio through echo effect
 * 
 * Applies delay with feedback and wet/dry mix to stereo audio.
 * Processing is done in-place.
 * 
 * @param echo Pointer to echo state
 * @param buffer Stereo interleaved int16 buffer (modified in-place)
 * @param num_frames Number of stereo frames to process
 * 
 * @note Thread Safety: Not thread-safe. Call from single audio task only.
 */
void echo_process(echo_t *echo, int16_t *buffer, size_t num_frames);

/**
 * @brief Reset echo buffer (clear delay line)
 * 
 * Call when switching tracks or seeking to prevent stale echoes.
 * 
 * @param echo Pointer to echo state
 */
void echo_reset(echo_t *echo);

#ifdef __cplusplus
}
#endif

#endif // ECHO_H
