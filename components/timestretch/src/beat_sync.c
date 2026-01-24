/**
 * @file beat_sync.c
 * @brief Beat synchronization implementation
 */

#include "beat_sync.h"
#include <math.h>
#include "esp_log.h"

static const char *TAG = "beat_sync";

static beat_grid_t s_grid = {0};

int beat_sync_init(const beat_grid_t *grid) {
    if (!grid) return -1;
    
    s_grid = *grid;
    
    ESP_LOGI(TAG, "Beat sync initialized: %.2f BPM, first beat @ %u ms", 
             s_grid.bpm, s_grid.first_beat_ms);
    
    return 0;
}

void beat_sync_update(uint32_t position_ms, beat_sync_state_t *state) {
    if (!state) return;
    
    state->current_position_ms = position_ms;
    state->current_bpm = s_grid.bpm;
    
    // Calculate milliseconds per beat
    float ms_per_beat = (60.0f * 1000.0f) / s_grid.bpm;
    
    // Calculate position relative to first beat
    int32_t relative_pos = (int32_t)position_ms - (int32_t)s_grid.first_beat_ms;
    if (relative_pos < 0) relative_pos = 0;
    
    // Find current beat number
    float beat_number = (float)relative_pos / ms_per_beat;
    uint32_t current_beat = (uint32_t)floorf(beat_number);
    
    // Calculate next beat position
    uint32_t next_beat_number = current_beat + 1;
    state->next_beat_ms = s_grid.first_beat_ms + 
                         (uint32_t)(next_beat_number * ms_per_beat);
    
    // Calculate phase error (-1.0 to 1.0)
    float position_in_beat = beat_number - current_beat;
    state->phase_error = (position_in_beat - 0.5f) * 2.0f; // -1.0 to 1.0
    
    state->is_synced = (fabsf(state->phase_error) < 0.1f);
}

uint32_t beat_sync_get_next_beat(uint32_t position_ms) {
    beat_sync_state_t state;
    beat_sync_update(position_ms, &state);
    return state.next_beat_ms;
}

float beat_sync_get_phase_error(uint32_t position_ms) {
    beat_sync_state_t state;
    beat_sync_update(position_ms, &state);
    return state.phase_error;
}

