/**
 * @file audio_fx.h
 * @brief Audio effects processing (EQ, filters, delay, reverb)
 */

#ifndef AUDIO_FX_H
#define AUDIO_FX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FX chain handle
 */
typedef struct audio_fx_chain_s audio_fx_chain_t;

/**
 * @brief FX types
 */
typedef enum {
    FX_TYPE_NONE = 0,
    FX_TYPE_LOWPASS,
    FX_TYPE_HIGHPASS,
    FX_TYPE_BANDPASS,
    FX_TYPE_EQ,
    FX_TYPE_DELAY,
    FX_TYPE_REVERB,
    FX_TYPE_FLANGER,
    FX_TYPE_GATER
} fx_type_t;

/**
 * @brief Create FX chain
 * 
 * @param sample_rate Sample rate (typically 44100)
 * @return FX chain handle or NULL on error
 */
audio_fx_chain_t *audio_fx_chain_create(uint32_t sample_rate);

/**
 * @brief Destroy FX chain
 * 
 * @param chain FX chain handle
 */
void audio_fx_chain_destroy(audio_fx_chain_t *chain);

/**
 * @brief Add effect to chain
 * 
 * @param chain FX chain handle
 * @param type Effect type
 * @param enabled Initially enabled
 * @return Effect ID or -1 on error
 */
int audio_fx_add(audio_fx_chain_t *chain, fx_type_t type, bool enabled);

/**
 * @brief Remove effect from chain
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 */
void audio_fx_remove(audio_fx_chain_t *chain, int fx_id);

/**
 * @brief Enable/disable effect
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param enabled True to enable
 */
void audio_fx_set_enabled(audio_fx_chain_t *chain, int fx_id, bool enabled);

/**
 * @brief Set lowpass filter parameters
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param cutoff_hz Cutoff frequency in Hz
 * @param resonance Q factor (0.5-10.0)
 */
void audio_fx_set_lowpass(audio_fx_chain_t *chain, int fx_id, 
                          float cutoff_hz, float resonance);

/**
 * @brief Set highpass filter parameters
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param cutoff_hz Cutoff frequency in Hz
 * @param resonance Q factor (0.5-10.0)
 */
void audio_fx_set_highpass(audio_fx_chain_t *chain, int fx_id, 
                           float cutoff_hz, float resonance);

/**
 * @brief Set EQ parameters
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param freq_hz Center frequency in Hz
 * @param gain_db Gain in dB (-12 to +12)
 * @param q Q factor (0.5-10.0)
 */
void audio_fx_set_eq(audio_fx_chain_t *chain, int fx_id, 
                     float freq_hz, float gain_db, float q);

/**
 * @brief Set delay parameters
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param delay_ms Delay time in milliseconds
 * @param feedback Feedback amount (0.0-0.95)
 * @param mix Mix amount (0.0-1.0)
 */
void audio_fx_set_delay(audio_fx_chain_t *chain, int fx_id, 
                        float delay_ms, float feedback, float mix);

/**
 * @brief Set reverb parameters
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param room_size Room size (0.0-1.0)
 * @param damping Damping (0.0-1.0)
 * @param mix Mix amount (0.0-1.0)
 */
void audio_fx_set_reverb(audio_fx_chain_t *chain, int fx_id, 
                         float room_size, float damping, float mix);

/**
 * @brief Set flanger parameters
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param rate_hz LFO rate in Hz
 * @param depth Depth (0.0-1.0)
 * @param feedback Feedback (0.0-0.9)
 * @param mix Mix amount (0.0-1.0)
 */
void audio_fx_set_flanger(audio_fx_chain_t *chain, int fx_id, 
                          float rate_hz, float depth, float feedback, float mix);

/**
 * @brief Set gater parameters (rhythmic mute)
 * 
 * @param chain FX chain handle
 * @param fx_id Effect ID
 * @param rate_hz Gate rate in Hz (sync to BPM)
 * @param duty_cycle Duty cycle (0.0-1.0)
 */
void audio_fx_set_gater(audio_fx_chain_t *chain, int fx_id, 
                        float rate_hz, float duty_cycle);

/**
 * @brief Process audio through FX chain
 * 
 * @param chain FX chain handle
 * @param input Input buffer (stereo interleaved)
 * @param output Output buffer (stereo interleaved)
 * @param num_samples Number of samples to process
 */
void audio_fx_process(audio_fx_chain_t *chain, 
                     const int16_t *input, 
                     int16_t *output, 
                     size_t num_samples);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_FX_H

