#include "beat_grid.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

beat_grid_result_t* beat_grid_generate(float bpm, uint32_t duration_ms, uint32_t first_beat_ms) {
    if (bpm <= 0 || duration_ms == 0) return NULL;
    
    beat_grid_result_t *result = calloc(1, sizeof(beat_grid_result_t));
    if (!result) return NULL;
    
    result->bpm = bpm;
    result->first_beat_ms = first_beat_ms;
    
    // Calculate beat interval
    float beat_interval_ms = 60000.0f / bpm;
    float beat_interval_sec = beat_interval_ms / 1000.0f;
    
    // Calculate number of beats
    float duration_from_first = duration_ms - first_beat_ms;
    size_t num_beats = (size_t)(duration_from_first / beat_interval_ms) + 1;
    
    // Allocate beat times array
    result->beat_times = malloc(num_beats * sizeof(float));
    if (!result->beat_times) {
        free(result);
        return NULL;
    }
    
    // Generate beat positions
    result->beat_count = 0;
    float time = first_beat_ms / 1000.0f;
    float end_time = duration_ms / 1000.0f;
    
    while (time <= end_time && result->beat_count < num_beats) {
        result->beat_times[result->beat_count++] = time;
        time += beat_interval_sec;
    }
    
    result->confidence = 0.8f;  // Default confidence
    
    return result;
}

uint32_t beat_grid_detect_first_beat(const int16_t *samples, size_t num_samples,
                                      uint32_t sample_rate, float bpm) {
    if (!samples || num_samples == 0 || bpm <= 0) return 0;
    
    // Look for first strong onset in the first few seconds
    // Use energy peaks similar to BPM detector
    
    float beat_interval_samples = (60.0f / bpm) * sample_rate;
    (void)beat_interval_samples;  // Reserved for future refinement
    
    size_t window_size = 1024;
    size_t max_search = sample_rate * 4;  // Search first 4 seconds
    if (max_search > num_samples / 2) max_search = num_samples / 2;
    
    float max_energy = 0;
    size_t max_pos = 0;
    
    // Find highest energy window (likely a downbeat)
    for (size_t i = 0; i + window_size * 2 <= max_search; i += window_size / 2) {
        float energy = 0;
        for (size_t j = 0; j < window_size; j++) {
            // Stereo to mono
            int32_t mono = (samples[(i + j) * 2] + samples[(i + j) * 2 + 1]) / 2;
            energy += (float)mono * mono;
        }
        
        if (energy > max_energy) {
            max_energy = energy;
            max_pos = i;
        }
    }
    
    // Convert sample position to milliseconds
    return (uint32_t)((float)max_pos * 1000.0f / sample_rate);
}

void beat_grid_free(beat_grid_result_t *grid) {
    if (grid) {
        free(grid->beat_times);
        free(grid);
    }
}

void beat_grid_to_seek_table(const beat_grid_result_t *grid, uint32_t duration_ms,
                             uint32_t seek_table[100]) {
    if (!grid || !seek_table) return;
    
    float duration_sec = duration_ms / 1000.0f;
    float beat_interval = 60.0f / grid->bpm;
    
    for (int i = 0; i < 100; i++) {
        // Target time for this percentage
        float target_time = (i / 100.0f) * duration_sec;
        
        // Find nearest beat
        if (grid->beat_count > 0 && beat_interval > 0) {
            float beats_from_start = (target_time - grid->first_beat_ms / 1000.0f) / beat_interval;
            float nearest_beat_num = roundf(beats_from_start);
            float aligned_time = (grid->first_beat_ms / 1000.0f) + nearest_beat_num * beat_interval;
            
            // Clamp to valid range
            if (aligned_time < 0) aligned_time = 0;
            if (aligned_time > duration_sec) aligned_time = duration_sec;
            
            seek_table[i] = (uint32_t)(aligned_time * 1000);
        } else {
            // No grid, just use linear position
            seek_table[i] = (uint32_t)(target_time * 1000);
        }
    }
}
