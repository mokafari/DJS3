/**
 * @file audio_output.h
 * @brief Audio output interface for PCM5102A DAC via I2S
 */

#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2S audio output for PCM5102A DAC
 * 
 * @return true on success, false on failure
 */
bool audio_output_init(void);

/**
 * @brief Deinitialize I2S audio output
 */
void audio_output_deinit(void);

/**
 * @brief Set sample rate
 * 
 * @param sample_rate Sample rate in Hz (typically 44100)
 * @return true on success, false on failure
 */
bool audio_output_set_rate(uint32_t sample_rate);

/**
 * @brief Get current sample rate
 * 
 * @return Sample rate in Hz
 */
uint32_t audio_output_get_rate(void);

/**
 * @brief Set number of channels
 * 
 * @param channels Number of channels (1 = mono, 2 = stereo)
 * @return true on success, false on failure
 */
bool audio_output_set_channels(uint8_t channels);

/**
 * @brief Set gain/volume
 * 
 * @param gain Gain value (0.0 to 4.0)
 * @return true on success, false on failure
 */
bool audio_output_set_gain(float gain);

#ifdef __cplusplus
}
// Forward declaration for C++ code
class AudioOutputI2S;
AudioOutputI2S* audio_output_get_i2s(void);
#endif

#endif /* AUDIO_OUTPUT_H */

