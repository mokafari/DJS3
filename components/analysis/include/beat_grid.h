#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Beat grid result
typedef struct {
    float bpm;                  // Detected/refined BPM
    uint32_t first_beat_ms;     // First downbeat position (ms)
    float *beat_times;          // Array of beat times (seconds)
    size_t beat_count;          // Number of beats
    float confidence;           // Grid confidence (0-1)
} beat_grid_result_t;

// Generate beat grid from BPM and track duration
// bpm: detected BPM
// duration_ms: track duration in milliseconds
// first_beat_ms: offset to first beat (0 = start, or detected)
beat_grid_result_t* beat_grid_generate(float bpm, uint32_t duration_ms, uint32_t first_beat_ms);

// Detect first beat position from audio
// Returns offset in milliseconds to first strong beat
uint32_t beat_grid_detect_first_beat(const int16_t *samples, size_t num_samples, 
                                      uint32_t sample_rate, float bpm);

// Free beat grid result
void beat_grid_free(beat_grid_result_t *grid);

// Get beat positions for seek table (100 points, 0-99% of track)
// Aligns to nearest beat boundary
void beat_grid_to_seek_table(const beat_grid_result_t *grid, uint32_t duration_ms,
                             uint32_t seek_table[100]);
