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
void waveform_view_update(const uint8_t *waveform_data, size_t num_samples, float position, size_t wave_index);
void waveform_view_update_grid(const float *beat_positions, size_t num_beats);
void waveform_view_show_cursor(float position, bool visible);
void waveform_view_reset(void);

/**
 * @brief Set waveform resolution divider for performance tuning
 * 
 * Higher divider = fewer bars = better performance, lower detail
 * 
 * @param divider Resolution divider (1=480 bars, 2=240, 4=120, 8=60)
 */
void waveform_view_set_resolution(int divider);

/**
 * @brief Get current waveform resolution divider
 * 
 * @return Current resolution divider (1-8)
 */
int waveform_view_get_resolution(void);

// ============================================================================
// PERFORMANCE PROFILING API
// ============================================================================

/**
 * @brief Get current waveform rendering FPS
 * 
 * @return Rolling average FPS, or -1 if profiling disabled
 */
float waveform_view_get_fps(void);

/**
 * @brief Get detailed performance statistics (rolling averages)
 * 
 * @param frame_us Average total frame time in microseconds
 * @param cache_us Average cache update time in microseconds
 * @param draw_us Average draw time in microseconds
 * @param invalidate_us Average invalidate time in microseconds
 */
void waveform_view_get_perf_stats(uint32_t *frame_us, uint32_t *cache_us, 
                                   uint32_t *draw_us, uint32_t *invalidate_us);

/**
 * @brief Reset performance counters
 */
void waveform_view_reset_perf(void);

#ifdef __cplusplus
}
#endif

#endif // WAVEFORM_VIEW_H

