/**
 * @file granular_engine.c
 * @brief Granular synthesis engine implementation
 */

#include "granular_engine.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "granular";

// Helper: Linear interpolation
static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Helper: Wrap position to buffer bounds
static float wrap_position(float pos, float max) {
    while (pos < 0) pos += max;
    while (pos >= max) pos -= max;
    return pos;
}

granular_params_t granular_engine_default_params(void) {
    granular_params_t params = {
        .grain_size_ms = 50.0f,
        .density_percent = 100.0f,
        .jitter_ms = 0.0f,
        .pitch_factor = 1.0f,
        .traverse_speed = 1.0f,
        .beat_sync_enabled = false,
        .freeze_mode = false
    };
    return params;
}

int granular_engine_init(granular_engine_t *engine, 
                         int16_t *audio_buffer, 
                         size_t buffer_size, 
                         uint32_t sample_rate) {
    if (!engine || !audio_buffer || buffer_size == 0) {
        return -1;
    }

    memset(engine, 0, sizeof(granular_engine_t));
    
    engine->audio_buffer = audio_buffer;
    engine->buffer_size = buffer_size;
    engine->sample_rate = sample_rate;
    engine->params = granular_engine_default_params();
    engine->bpm = 120.0f;
    engine->sync_enabled = false;
    
    // Initialize state
    engine->state.read_head = 0.0f;
    engine->state.file_position = 0.0f;
    engine->state.grain_start = 0.0f;
    engine->state.master_phase = 0.0f;
    engine->state.samples_per_beat = (double)sample_rate * 60.0 / engine->bpm;
    engine->state.grain_active = true;
    
    ESP_LOGI(TAG, "Granular engine initialized: %zu samples @ %u Hz", 
             buffer_size, sample_rate);
    
    return 0;
}

void granular_engine_set_params(granular_engine_t *engine, 
                                const granular_params_t *params) {
    if (!engine || !params) return;
    
    // Clamp parameters to valid ranges
    engine->params.grain_size_ms = fmaxf(10.0f, fminf(200.0f, params->grain_size_ms));
    engine->params.density_percent = fmaxf(25.0f, params->density_percent);
    engine->params.jitter_ms = fmaxf(0.0f, fminf(50.0f, params->jitter_ms));
    engine->params.pitch_factor = fmaxf(0.5f, fminf(2.0f, params->pitch_factor));
    engine->params.traverse_speed = fmaxf(0.0f, fminf(2.0f, params->traverse_speed));
    engine->params.beat_sync_enabled = params->beat_sync_enabled;
    engine->params.freeze_mode = params->freeze_mode;
}

void granular_engine_set_bpm(granular_engine_t *engine, float bpm) {
    if (!engine) return;
    
    engine->bpm = fmaxf(60.0f, fminf(180.0f, bpm));
    engine->state.samples_per_beat = (double)engine->sample_rate * 60.0 / engine->bpm;
}

void granular_engine_set_sync(granular_engine_t *engine, bool enabled) {
    if (!engine) return;
    engine->sync_enabled = enabled;
    engine->params.beat_sync_enabled = enabled;
}

void granular_engine_set_position(granular_engine_t *engine, uint32_t position) {
    if (!engine) return;
    
    float pos = (float)position;
    if (pos >= engine->buffer_size) pos = engine->buffer_size - 1;
    
    engine->state.file_position = pos;
    engine->state.read_head = pos;
    engine->state.grain_start = pos;
}

uint32_t granular_engine_get_position(const granular_engine_t *engine) {
    if (!engine) return 0;
    return (uint32_t)engine->state.file_position;
}

void granular_engine_reset_grain(granular_engine_t *engine) {
    if (!engine) return;
    
    // Apply jitter if enabled
    float jitter_samples = 0.0f;
    if (engine->params.jitter_ms > 0.0f) {
        jitter_samples = ((float)rand() / RAND_MAX) * 
                         (engine->params.jitter_ms * engine->sample_rate / 1000.0f);
        jitter_samples -= (engine->params.jitter_ms * engine->sample_rate / 2000.0f);
    }
    
    engine->state.grain_start = wrap_position(
        engine->state.file_position + jitter_samples, 
        engine->buffer_size
    );
    engine->state.read_head = engine->state.grain_start;
    engine->grain_counter = 0;
}

void granular_engine_process(granular_engine_t *engine, 
                             int16_t *output, 
                             size_t num_samples) {
    if (!engine || !output || num_samples == 0) return;
    
    const float sample_rate = (float)engine->sample_rate;
    const float grain_size_samples = (engine->params.grain_size_ms * sample_rate) / 1000.0f;
    const float density_factor = engine->params.density_percent / 100.0f;
    
    // Calculate samples per 16th note for beat sync
    const double samples_per_16th = engine->state.samples_per_beat / 4.0;
    
    for (size_t i = 0; i < num_samples; i++) {
        // Advance master clock (for beat sync)
        if (engine->sync_enabled && engine->params.beat_sync_enabled) {
            engine->state.master_phase += 1.0;
            
            // Beat sync event: reset grain on 16th note boundaries
            if (engine->state.master_phase >= samples_per_16th) {
                engine->state.master_phase = 0.0;
                granular_engine_reset_grain(engine);
            }
        }
        
        // Traverse file position (time-stretch)
        if (!engine->params.freeze_mode) {
            engine->state.file_position += engine->params.traverse_speed;
            engine->state.file_position = wrap_position(
                engine->state.file_position, 
                engine->buffer_size
            );
        }
        
        // Check if grain has finished (duration-based restart)
        if (!engine->sync_enabled || !engine->params.beat_sync_enabled) {
            float grain_elapsed = engine->state.read_head - engine->state.grain_start;
            if (grain_elapsed < 0) grain_elapsed += engine->buffer_size;
            
            if (grain_elapsed >= grain_size_samples) {
                granular_engine_reset_grain(engine);
            }
        }
        
        // Read sample with linear interpolation
        float read_pos = wrap_position(engine->state.read_head, engine->buffer_size);
        int32_t idx0 = (int32_t)read_pos;
        int32_t idx1 = (idx0 + 1) % engine->buffer_size;
        float frac = read_pos - idx0;
        
        int16_t sample0 = engine->audio_buffer[idx0];
        int16_t sample1 = engine->audio_buffer[idx1];
        float sample = lerp((float)sample0, (float)sample1, frac);
        
        // Apply density/overlap (simple gain for now, can be enhanced with multiple grains)
        sample *= fminf(1.0f, density_factor);
        
        // Clamp and output (stereo)
        int16_t out_sample = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, sample));
        output[i * 2] = out_sample;     // Left
        output[i * 2 + 1] = out_sample; // Right
        
        // Advance read head by pitch factor
        engine->state.read_head += engine->params.pitch_factor;
        engine->state.read_head = wrap_position(
            engine->state.read_head, 
            engine->buffer_size
        );
        
        engine->grain_counter++;
    }
}

