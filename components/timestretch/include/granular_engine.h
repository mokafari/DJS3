/**
 * @file granular_engine.h
 * @brief Granular synthesis engine for time-stretching and creative audio manipulation
 * 
 * This engine implements a beat-synced granular synthesis algorithm that allows
 * for time-stretching while maintaining beat alignment. It exposes parameters for
 * creative sound design (metallic ringing, lush scapes, glitchy madness).
 * 
 * Features:
 * - Multi-grain synthesis with overlapping grains for density effects
 * - Window functions (Hann/Hamming) to prevent clicks
 * - Beat-synced grid-locked freezing
 * - Per-grain jitter for glitch effects
 */

#ifndef GRANULAR_ENGINE_H
#define GRANULAR_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of simultaneous grains
 */
#define GRANULAR_MAX_GRAINS 16

/**
 * @brief Window function type
 */
typedef enum {
    GRANULAR_WINDOW_HANN = 0,     ///< Hann window (smooth, musical)
    GRANULAR_WINDOW_HAMMING,      ///< Hamming window (slightly brighter)
    GRANULAR_WINDOW_TRIANGLE,     ///< Triangle window (sharp attack)
    GRANULAR_WINDOW_RECTANGLE     ///< Rectangle (no window, for glitch)
} granular_window_t;

/**
 * @brief Granular engine mode
 */
typedef enum {
    GRANULAR_MODE_MULTI_GRAIN = 0,  ///< Multi-grain synthesis mode (existing)
    GRANULAR_MODE_STREAMING          ///< Simple grain loop streaming mode (as per spec)
} granular_mode_t;

/**
 * @brief Granular engine parameters
 */
typedef struct {
    float grain_size_ms;      ///< Grain size in milliseconds (10-200ms)
    float density_percent;    ///< Grain density/overlap (25-300%+)
    float jitter_ms;          ///< Random offset to grain start (0-50ms)
    float pitch_factor;       ///< Pitch multiplier (0.5-2.0)
    float traverse_speed;     ///< File traversal speed (0.0-2.0)
    bool beat_sync_enabled;   ///< Enable beat-synced grain restart
    bool freeze_mode;         ///< Freeze current position (infinite loop)
    granular_window_t window; ///< Window function type
} granular_params_t;

/**
 * @brief Individual grain state
 */
typedef struct {
    float read_pos;           ///< Current read position in buffer (samples)
    float start_pos;          ///< Grain start position (samples)
    float age;                ///< Grain age in samples (0 to grain_size)
    bool active;              ///< Is grain currently active
    float jitter_offset;      ///< Per-grain jitter offset
} grain_t;

/**
 * @brief Granular engine state
 */
typedef struct {
    grain_t grains[GRANULAR_MAX_GRAINS]; ///< Active grains
    float file_position;       ///< Virtual file position (for time-stretch)
    float master_phase;        ///< Master clock phase (0.0 to samples_per_beat)
    double samples_per_beat;  ///< Samples per beat (calculated from BPM)
    uint32_t next_beat_sample; ///< Next beat boundary in samples
    float current_beat_start;  ///< Start position of current beat
} granular_state_t;

/**
 * @brief Streaming mode state (for simple grain loop)
 */
typedef struct {
    double read_index;        ///< Current read position (granular head)
    double grain_start;       ///< Current grain start position
    float speed;              ///< Playback speed (0.0 = freeze, 1.0 = normal)
    float grain_size_samples; ///< Grain size in samples
    float jitter_amount;     ///< Jitter amount (0.0 = none)
} streaming_state_t;

/**
 * @brief Granular engine handle
 */
typedef struct granular_engine_s {
    int16_t *audio_buffer;    ///< Audio data buffer (in PSRAM)
    size_t buffer_size;        ///< Buffer size in samples
    uint32_t sample_rate;      ///< Sample rate (typically 44100)
    
    granular_params_t params;  ///< Current parameters
    granular_state_t state;    ///< Current state
    
    // Streaming mode
    granular_mode_t mode;       ///< Current mode (multi-grain or streaming)
    int32_t write_index;       ///< Write head position (where MP3 decoder writes)
    streaming_state_t streaming; ///< Streaming mode state
    
    // Beat sync
    float bpm;                 ///< Master BPM
    bool sync_enabled;         ///< Beat sync enabled
    
    // Internal counters
    uint32_t grain_counter;    ///< Grain playback counter
    float grain_spawn_counter; ///< Counter for grain spawning (non-beat-sync mode)
} granular_engine_t;

/**
 * @brief Initialize granular engine
 * 
 * @param engine Engine handle
 * @param audio_buffer Audio data buffer (must be in PSRAM)
 * @param buffer_size Buffer size in samples
 * @param sample_rate Sample rate (typically 44100)
 * @return 0 on success, negative on error
 */
int granular_engine_init(granular_engine_t *engine, 
                         int16_t *audio_buffer, 
                         size_t buffer_size, 
                         uint32_t sample_rate);

/**
 * @brief Set granular parameters
 * 
 * @param engine Engine handle
 * @param params Parameters structure
 */
void granular_engine_set_params(granular_engine_t *engine, 
                                const granular_params_t *params);

/**
 * @brief Set master BPM for beat sync
 * 
 * @param engine Engine handle
 * @param bpm BPM value (60-180)
 */
void granular_engine_set_bpm(granular_engine_t *engine, float bpm);

/**
 * @brief Enable/disable beat sync
 * 
 * @param engine Engine handle
 * @param enabled True to enable beat sync
 */
void granular_engine_set_sync(granular_engine_t *engine, bool enabled);

/**
 * @brief Process audio block (generate output samples)
 * 
 * @param engine Engine handle
 * @param output Output buffer (stereo interleaved)
 * @param num_samples Number of samples to generate
 */
void granular_engine_process(granular_engine_t *engine, 
                            int16_t *output, 
                            size_t num_samples);

/**
 * @brief Set file position (for seeking)
 * 
 * @param engine Engine handle
 * @param position Position in samples
 */
void granular_engine_set_position(granular_engine_t *engine, uint32_t position);

/**
 * @brief Get current read position
 * 
 * @param engine Engine handle
 * @return Current read position in samples
 */
uint32_t granular_engine_get_position(const granular_engine_t *engine);

/**
 * @brief Reset grain (for manual triggering)
 * 
 * @param engine Engine handle
 */
void granular_engine_reset_grain(granular_engine_t *engine);

/**
 * @brief Get default parameters
 * 
 * @return Default parameters structure
 */
granular_params_t granular_engine_default_params(void);

/**
 * @brief Calculate window function value
 * 
 * @param window Window function type
 * @param position Position in grain (0.0 to 1.0)
 * @return Window value (0.0 to 1.0)
 */
float granular_window_value(granular_window_t window, float position);

/**
 * @brief Get number of active grains
 * 
 * @param engine Engine handle
 * @return Number of active grains
 */
uint32_t granular_engine_get_active_grain_count(const granular_engine_t *engine);

/**
 * @brief Set granular engine mode (multi-grain or streaming)
 * 
 * @param engine Engine handle
 * @param mode Mode to use
 */
void granular_engine_set_mode(granular_engine_t *engine, granular_mode_t mode);

/**
 * @brief Write chunk of decoded audio to buffer (for streaming mode)
 * 
 * @param engine Engine handle
 * @param data Audio data (stereo interleaved, 16-bit)
 * @param samples Number of samples to write (mono samples, so stereo = samples*2)
 * @return Number of samples written
 */
size_t granular_engine_write_chunk(granular_engine_t *engine, const int16_t *data, size_t samples);

/**
 * @brief Get current write index (for buffer distance checking)
 * 
 * @param engine Engine handle
 * @return Write index in samples
 */
int32_t granular_engine_get_write_index(const granular_engine_t *engine);

/**
 * @brief Get current read index (for buffer distance checking)
 * 
 * @param engine Engine handle
 * @return Read index in samples
 */
int32_t granular_engine_get_read_index(const granular_engine_t *engine);

/**
 * @brief Check buffer distance and determine if decoder should pause/resume
 * 
 * @param engine Engine handle
 * @param loop_limit Maximum distance before pausing (in samples)
 * @param refill_threshold Minimum distance before resuming (in samples)
 * @return true if decoder should pause, false if should resume/continue
 */
bool granular_engine_check_buffer_distance(const granular_engine_t *engine, 
                                          int32_t loop_limit, 
                                          int32_t refill_threshold);

/**
 * @brief Set streaming mode parameters (speed, grain size, jitter)
 * 
 * @param engine Engine handle
 * @param speed Playback speed (0.0 = freeze, 1.0 = normal, >1.0 = faster)
 * @param grain_size_samples Grain size in samples
 * @param jitter Jitter amount (0.0 = none)
 */
void granular_engine_set_streaming_params(granular_engine_t *engine, 
                                         float speed, 
                                         float grain_size_samples, 
                                         float jitter);

#ifdef __cplusplus
}
#endif

#endif // GRANULAR_ENGINE_H

