/**
 * @file slip_loop.h
 * @brief Slip loop engine with circular buffer for short stutters
 * 
 * Implements Pioneer CDJ-style slip mode: loop a section while the track
 * continues playing in the background, then jump back to the correct position.
 * 
 * Features:
 * - Regular mode: Standard looping behavior
 * - DJFX mode: Pitch feedback - shortening loop increases pitch, lengthening decreases pitch
 * - Reverse: Play loop backwards
 * - Scatter: Random position jumps within buffer for glitch effects
 */

#ifndef SLIP_LOOP_H
#define SLIP_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Slip loop mode
 */
typedef enum {
    SLIP_LOOP_MODE_OFF = 0,      ///< No slip loop active
    SLIP_LOOP_MODE_TIME,          ///< Time-based loop (milliseconds)
    SLIP_LOOP_MODE_BEAT           ///< Beat-synced loop (beats)
} slip_loop_mode_t;

/**
 * @brief Playback mode
 */
typedef enum {
    SLIP_PLAYBACK_REGULAR = 0,   ///< Regular looping (no pitch change)
    SLIP_PLAYBACK_DJFX            ///< DJFX mode (pitch feedback based on loop length)
} slip_playback_mode_t;

/**
 * @brief Slip loop handle
 */
typedef struct slip_loop_s {
    int16_t *circular_buffer;     ///< Circular buffer for loop storage
    size_t buffer_size_samples;   ///< Buffer size in samples
    uint32_t sample_rate;         ///< Sample rate
    
    // Loop parameters
    slip_loop_mode_t mode;        ///< Current mode
    slip_playback_mode_t playback_mode; ///< Playback mode (regular or DJFX)
    uint32_t loop_length_ms;      ///< Loop length in milliseconds (time mode)
    uint32_t loop_length_beats;   ///< Loop length in beats (beat mode)
    uint32_t base_length_ms;      ///< Base loop length for DJFX pitch calculation
    float bpm;                     ///< Current BPM (for beat mode)
    
    // Playback options
    bool reverse;                  ///< Play loop in reverse
    bool scatter;                  ///< Random position jumps (scatter mode)
    float scatter_probability;    ///< Probability of scatter jump (0.0 to 1.0)
    
    // Playback state
    bool is_active;                ///< Is slip loop currently active?
    uint32_t loop_start_pos;      ///< Start position in main buffer (samples)
    uint32_t loop_end_pos;        ///< End position in main buffer (samples)
    uint32_t loop_read_pos;       ///< Current read position in loop (samples)
    float read_pos_frac;          ///< Fractional read position for DJFX pitch shifting
    
    // Background playback
    uint32_t background_pos;      ///< Where track would be if not looping (samples)
    uint32_t background_start_pos; ///< Position when loop started (samples)
    uint64_t loop_start_time_ms;  ///< Timestamp when loop started
    
    // Buffer management
    uint32_t write_pos;           ///< Current write position in circular buffer
    bool buffer_filled;            ///< Has buffer been filled at least once?
} slip_loop_t;

/**
 * @brief Initialize slip loop engine
 * 
 * @param slip Slip loop handle
 * @param buffer_size_samples Circular buffer size in samples
 * @param sample_rate Sample rate (typically 44100)
 * @return 0 on success, negative on error
 */
int slip_loop_init(slip_loop_t *slip, size_t buffer_size_samples, uint32_t sample_rate);

/**
 * @brief Deinitialize slip loop engine
 * 
 * @param slip Slip loop handle
 */
void slip_loop_deinit(slip_loop_t *slip);

/**
 * @brief Set BPM (for beat-synced mode)
 * 
 * @param slip Slip loop handle
 * @param bpm BPM value (60-180)
 */
void slip_loop_set_bpm(slip_loop_t *slip, float bpm);

/**
 * @brief Set playback mode (regular or DJFX)
 * 
 * @param slip Slip loop handle
 * @param playback_mode Playback mode
 */
void slip_loop_set_playback_mode(slip_loop_t *slip, slip_playback_mode_t playback_mode);

/**
 * @brief Enable/disable reverse playback
 * 
 * @param slip Slip loop handle
 * @param reverse True to play in reverse
 */
void slip_loop_set_reverse(slip_loop_t *slip, bool reverse);

/**
 * @brief Enable/disable scatter mode
 * 
 * @param slip Slip loop handle
 * @param scatter True to enable scatter mode
 * @param probability Probability of scatter jump (0.0 to 1.0)
 */
void slip_loop_set_scatter(slip_loop_t *slip, bool scatter, float probability);

/**
 * @brief Update loop length (for DJFX mode pitch feedback)
 * 
 * @param slip Slip loop handle
 * @param length_ms New loop length in milliseconds
 */
void slip_loop_update_length(slip_loop_t *slip, uint32_t length_ms);

/**
 * @brief Start slip loop (time-based)
 * 
 * @param slip Slip loop handle
 * @param start_pos Start position in main buffer (samples)
 * @param length_ms Loop length in milliseconds
 * @return 0 on success, negative on error
 */
int slip_loop_start_time(slip_loop_t *slip, uint32_t start_pos, uint32_t length_ms);

/**
 * @brief Start slip loop (beat-synced)
 * 
 * @param slip Slip loop handle
 * @param start_pos Start position in main buffer (samples)
 * @param length_beats Loop length in beats (1, 2, 4, 8, etc.)
 * @return 0 on success, negative on error
 */
int slip_loop_start_beat(slip_loop_t *slip, uint32_t start_pos, uint32_t length_beats);

/**
 * @brief Stop slip loop and jump to background position
 * 
 * @param slip Slip loop handle
 * @return Background position (where track would be)
 */
uint32_t slip_loop_stop(slip_loop_t *slip);

/**
 * @brief Check if slip loop is active
 * 
 * @param slip Slip loop handle
 * @return True if active
 */
bool slip_loop_is_active(const slip_loop_t *slip);

/**
 * @brief Process audio through slip loop
 * 
 * When active, this reads from the loop buffer. When inactive, it records
 * to the circular buffer for future loops.
 * 
 * @param slip Slip loop handle
 * @param main_buffer Main audio buffer
 * @param main_buffer_size Main buffer size in samples
 * @param output Output buffer (stereo interleaved)
 * @param num_samples Number of samples to process
 */
void slip_loop_process(slip_loop_t *slip,
                      const int16_t *main_buffer,
                      size_t main_buffer_size,
                      int16_t *output,
                      size_t num_samples);

/**
 * @brief Get current loop position (0.0 to 1.0)
 * 
 * @param slip Slip loop handle
 * @return Position within loop (0.0 to 1.0)
 */
float slip_loop_get_position(const slip_loop_t *slip);

/**
 * @brief Get background position (where track would be)
 * 
 * @param slip Slip loop handle
 * @return Background position in samples
 */
uint32_t slip_loop_get_background_pos(const slip_loop_t *slip);

#ifdef __cplusplus
}
#endif

#endif // SLIP_LOOP_H

