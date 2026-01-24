/**
 * @file waveform_view.h
 * @brief Waveform view interface
 */

#ifndef WAVEFORM_VIEW_H
#define WAVEFORM_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void waveform_view_init(uint32_t width, uint32_t height);
void waveform_view_show(void);
void waveform_view_hide(void);
void waveform_view_update(const uint8_t *waveform_data, size_t num_samples, float position);
void waveform_view_update_grid(const float *beat_positions, size_t num_beats);
void waveform_view_show_cursor(float position, bool visible);

#ifdef __cplusplus
}
#endif

#endif // WAVEFORM_VIEW_H

