/**
 * @file granular_engine.h
 * @brief Granular synthesis engine for time-stretching and creative audio manipulation
 * 
 * This engine implements a beat-synced granular synthesis algorithm that allows
 * for time-stretching while maintaining beat alignment. It exposes parameters for
 * creative sound design (metallic ringing, lush scapes, glitchy madness).
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
 * @brief Granular engine parameters
 */
typedef struct {
    float grain_size_ms;      ///< Grain size in milliseconds (10-200ms)
    float density_percent;    ///< Grain density/overlap (25-300%+)
    float jitter_ms;          ///< Random offset to grain start (0-50ms)
    float pitch_factor;       ///< Pitch multiplier (0.5-2.0)
    float traverse_speed;     ///< File traversal speed (0.0-2.0)
    bool beat_sync_enabled;   ///< Enable beat-synced grain restart
    bool freeze_mode;          ///< Freeze current position (infinite loop)
} granular_params_t;

/**
 * @brief Granular engine state
 */
typedef struct {
    float read_head;          ///< Current audio read position (samples)
    float file_position;       ///< Virtual file position (for time-stretch)
    float grain_start;         ///< Current grain start position
    float master_phase;        ///< Master clock phase (0.0 to samples_per_beat)
    double samples_per_beat;  ///< Samples per beat (calculated from BPM)
    bool grain_active;         ///< Is grain currently playing
} granular_state_t;

/**
 * @brief Granular engine handle
 */
typedef struct granular_engine_s {
    int16_t *audio_buffer;    ///< Audio data buffer (in PSRAM)
    size_t buffer_size;        ///< Buffer size in samples
    uint32_t sample_rate;      ///< Sample rate (typically 44100)
    
    granular_params_t params;  ///< Current parameters
    granular_state_t state;    ///< Current state
    
    // Beat sync
    float bpm;                 ///< Master BPM
    bool sync_enabled;         ///< Beat sync enabled
    
    // Internal counters
    uint32_t grain_counter;    ///< Grain playback counter
    float jitter_accumulator;   ///< Jitter random offset
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

#ifdef __cplusplus
}
#endif

#endif // GRANULAR_ENGINE_H

