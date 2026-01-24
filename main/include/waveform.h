/**
 * @file waveform.h
 * @brief Real-time waveform visualization with cue point markers
 */

#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAVEFORM_WIDTH  400  // Waveform display width
#define WAVEFORM_HEIGHT 100  // Waveform display height
#define WAVEFORM_SAMPLES 512 // Number of samples for FFT

/**
 * @brief Initialize waveform system
 * 
 * @return true on success, false on failure
 */
bool waveform_init(void);

/**
 * @brief Update waveform with audio data
 * 
 * @param audio_samples Audio sample buffer (16-bit PCM)
 * @param num_samples Number of samples
 * @param position Current playback position in seconds
 * @param duration Total track duration in seconds
 */
void waveform_update(int16_t *audio_samples, uint32_t num_samples,
                     uint32_t position, uint32_t duration);

/**
 * @brief Render waveform to display buffer
 * 
 * @param x X position on display
 * @param y Y position on display
 * @param width Width of waveform area
 * @param height Height of waveform area
 */
void waveform_render(int x, int y, int width, int height);

/**
 * @brief Set playhead position
 * 
 * @param position Position in seconds
 */
void waveform_set_playhead(uint32_t position);

/**
 * @brief Set track duration
 * 
 * @param duration Duration in seconds
 */
void waveform_set_duration(uint32_t duration);

/**
 * @brief Add cue point marker
 * 
 * @param cue_id Cue point ID (0-7)
 * @param position Position in seconds
 * @param color RGB565 color for marker
 */
void waveform_add_cue_marker(uint8_t cue_id, uint32_t position, uint16_t color);

/**
 * @brief Remove cue point marker
 * 
 * @param cue_id Cue point ID
 */
void waveform_remove_cue_marker(uint8_t cue_id);

/**
 * @brief Clear all cue markers
 */
void waveform_clear_cue_markers(void);

#ifdef __cplusplus
}
#endif

#endif /* WAVEFORM_H */
