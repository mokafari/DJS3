/**
 * @file granular_engine.c
 * @brief Granular synthesis engine implementation
 * 
 * Enhanced multi-grain synthesis engine with:
 * - Overlapping grains for density/overlap effects
 * - Window functions to prevent clicks
 * - Beat-synced grid-locked freezing
 * - Per-grain jitter for glitch effects
 */

#include "granular_engine.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "granular";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

// Helper: Random float between 0 and 1
static float random_float(void) {
    return (float)rand() / (float)RAND_MAX;
}

// Helper: Random float between -1 and 1
static float random_float_signed(void) {
    return (random_float() * 2.0f) - 1.0f;
}

/**
 * @brief Calculate window function value
 */
float granular_window_value(granular_window_t window, float position) {
    // Clamp position to [0, 1]
    if (position < 0.0f) position = 0.0f;
    if (position > 1.0f) position = 1.0f;
    
    switch (window) {
        case GRANULAR_WINDOW_HANN:
            // Hann window: 0.5 * (1 - cos(2πx))
            return 0.5f * (1.0f - cosf(2.0f * M_PI * position));
            
        case GRANULAR_WINDOW_HAMMING:
            // Hamming window: 0.54 - 0.46 * cos(2πx)
            return 0.54f - 0.46f * cosf(2.0f * M_PI * position);
            
        case GRANULAR_WINDOW_TRIANGLE:
            // Triangle window
            if (position < 0.5f) {
                return position * 2.0f;
            } else {
                return 2.0f * (1.0f - position);
            }
            
        case GRANULAR_WINDOW_RECTANGLE:
        default:
            return 1.0f;
    }
}

granular_params_t granular_engine_default_params(void) {
    granular_params_t params = {
        .grain_size_ms = 50.0f,
        .density_percent = 100.0f,
        .jitter_ms = 0.0f,
        .pitch_factor = 1.0f,
        .traverse_speed = 1.0f,
        .beat_sync_enabled = false,
        .freeze_mode = false,
        .window = GRANULAR_WINDOW_HANN
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
    engine->mode = GRANULAR_MODE_MULTI_GRAIN; // Default to multi-grain mode
    
    // Initialize streaming mode state
    engine->write_index = 0;
    engine->streaming.read_index = 0.0;
    engine->streaming.grain_start = 0.0;
    engine->streaming.speed = 1.0f;
    engine->streaming.grain_size_samples = (engine->params.grain_size_ms * sample_rate) / 1000.0f;
    engine->streaming.jitter_amount = 0.0f;
    
    // Initialize state
    engine->state.file_position = 0.0f;
    engine->state.master_phase = 0.0;
    engine->state.samples_per_beat = (double)sample_rate * 60.0 / engine->bpm;
    engine->state.next_beat_sample = (uint32_t)engine->state.samples_per_beat;
    engine->state.current_beat_start = 0.0f;
    
    // Initialize all grains as inactive
    for (int i = 0; i < GRANULAR_MAX_GRAINS; i++) {
        engine->state.grains[i].active = false;
        engine->state.grains[i].age = 0.0f;
        engine->state.grains[i].jitter_offset = 0.0f;
    }
    
    engine->grain_counter = 0;
    engine->grain_spawn_counter = 0.0f;
    
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
    engine->params.window = params->window;
}

void granular_engine_set_bpm(granular_engine_t *engine, float bpm) {
    if (!engine) return;
    
    engine->bpm = fmaxf(60.0f, fminf(180.0f, bpm));
    engine->state.samples_per_beat = (double)engine->sample_rate * 60.0 / engine->bpm;
    engine->state.next_beat_sample = (uint32_t)engine->state.samples_per_beat;
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
    engine->state.current_beat_start = pos;
    
    // Reset all grains
    for (int i = 0; i < GRANULAR_MAX_GRAINS; i++) {
        engine->state.grains[i].active = false;
    }
}

uint32_t granular_engine_get_position(const granular_engine_t *engine) {
    if (!engine) return 0;
    return (uint32_t)engine->state.file_position;
}

/**
 * @brief Find an inactive grain slot
 */
static int find_inactive_grain(granular_engine_t *engine) {
    for (int i = 0; i < GRANULAR_MAX_GRAINS; i++) {
        if (!engine->state.grains[i].active) {
            return i;
        }
    }
    return -1; // All grains active
}

/**
 * @brief Start a new grain at the current file position
 */
static void start_new_grain(granular_engine_t *engine, float base_position) {
    int grain_idx = find_inactive_grain(engine);
    if (grain_idx < 0) {
        // All grains active, reuse oldest grain
        float oldest_age = 0.0f;
        int oldest_idx = 0;
        for (int i = 0; i < GRANULAR_MAX_GRAINS; i++) {
            if (engine->state.grains[i].age > oldest_age) {
                oldest_age = engine->state.grains[i].age;
                oldest_idx = i;
            }
        }
        grain_idx = oldest_idx;
    }
    
    grain_t *grain = &engine->state.grains[grain_idx];
    
    // Apply jitter
    float jitter_samples = 0.0f;
    if (engine->params.jitter_ms > 0.0f) {
        jitter_samples = random_float_signed() * 
                         (engine->params.jitter_ms * engine->sample_rate / 1000.0f);
    }
    
    grain->start_pos = wrap_position(base_position + jitter_samples, engine->buffer_size);
    grain->read_pos = grain->start_pos;
    grain->age = 0.0f;
    grain->active = true;
    grain->jitter_offset = jitter_samples;
}

void granular_engine_reset_grain(granular_engine_t *engine) {
    if (!engine) return;
    
    // Start new grain at current file position
    start_new_grain(engine, engine->state.file_position);
}

uint32_t granular_engine_get_active_grain_count(const granular_engine_t *engine) {
    if (!engine) return 0;
    
    uint32_t count = 0;
    for (int i = 0; i < GRANULAR_MAX_GRAINS; i++) {
        if (engine->state.grains[i].active) {
            count++;
        }
    }
    return count;
}

// Forward declarations for static functions
static void process_streaming_mode(granular_engine_t *engine, 
                                  int16_t *output, 
                                  size_t num_samples);
static void process_multi_grain_mode(granular_engine_t *engine, 
                                    int16_t *output, 
                                    size_t num_samples);

void granular_engine_process(granular_engine_t *engine, 
                             int16_t *output, 
                             size_t num_samples) {
    if (!engine || !output || num_samples == 0) return;
    
    // Check mode and process accordingly
    if (engine->mode == GRANULAR_MODE_STREAMING) {
        // Simple grain loop mode (as per spec)
        process_streaming_mode(engine, output, num_samples);
    } else {
        // Multi-grain synthesis mode (existing)
        process_multi_grain_mode(engine, output, num_samples);
    }
}

/**
 * @brief Process audio in streaming mode (simple grain loop)
 */
static void process_streaming_mode(granular_engine_t *engine, 
                                  int16_t *output, 
                                  size_t num_samples) {
    const float pitch = engine->params.pitch_factor;
    const float speed = engine->streaming.speed;
    const float grain_size = engine->streaming.grain_size_samples;
    
    for (size_t i = 0; i < num_samples; i++) {
        // 1. Calculate read position
        int32_t idx = (int32_t)engine->streaming.read_index;
        
        // 2. Read sample from buffer
        idx = idx % engine->buffer_size;
        if (idx < 0) idx += engine->buffer_size;
        
        int16_t sample = engine->audio_buffer[idx];
        
        // Output stereo (dual mono for now)
        output[i * 2] = sample;     // Left
        output[i * 2 + 1] = sample; // Right
        
        // 3. Move read head
        engine->streaming.read_index += pitch;
        
        // 4. Granular loop check
        double dist = engine->streaming.read_index - engine->streaming.grain_start;
        
        if (dist > grain_size || dist < 0) {
            // Reset grain
            // In streaming mode, grainStart moves forward slowly
            engine->streaming.grain_start += (speed * 0.5);
            
            // Add jitter
            if (engine->streaming.jitter_amount > 0.0f) {
                float jitter_offset = (random_float_signed() * 500.0f) * engine->streaming.jitter_amount;
                engine->streaming.grain_start += jitter_offset;
            }
            
            // Snap read head back to grain start
            engine->streaming.read_index = engine->streaming.grain_start;
        }
        
        // 5. Circular wrap safety
        if (engine->streaming.read_index >= engine->buffer_size) {
            engine->streaming.read_index -= engine->buffer_size;
        }
        if (engine->streaming.read_index < 0) {
            engine->streaming.read_index += engine->buffer_size;
        }
        
        if (engine->streaming.grain_start >= engine->buffer_size) {
            engine->streaming.grain_start -= engine->buffer_size;
        }
        if (engine->streaming.grain_start < 0) {
            engine->streaming.grain_start += engine->buffer_size;
        }
    }
}

/**
 * @brief Process audio in multi-grain mode (existing implementation)
 */
static void process_multi_grain_mode(granular_engine_t *engine, 
                                     int16_t *output, 
                                     size_t num_samples) {
    const float sample_rate = (float)engine->sample_rate;
    const float grain_size_samples = (engine->params.grain_size_ms * sample_rate) / 1000.0f;
    const float density_factor = engine->params.density_percent / 100.0f;
    
    // Calculate grain spacing based on density
    // At 100% density, grains start every grain_size_samples
    // At 300% density, grains start every grain_size_samples/3
    float grain_spacing = grain_size_samples / density_factor;
    
    // Calculate samples per beat for beat sync
    const double samples_per_beat = engine->state.samples_per_beat;
    // const double samples_per_16th = samples_per_beat / 4.0; // Reserved for future use
    
    for (size_t i = 0; i < num_samples; i++) {
        // Advance master clock (for beat sync)
        if (engine->sync_enabled && engine->params.beat_sync_enabled) {
            engine->state.master_phase += 1.0;
            
            // Beat sync event: snap to next beat boundary
            if (engine->state.master_phase >= samples_per_beat) {
                engine->state.master_phase = 0.0;
                
                // In freeze mode, jump to next beat start position
                if (engine->params.freeze_mode) {
                    // Calculate next beat start in file
                    float beat_duration_samples = (float)samples_per_beat;
                    engine->state.current_beat_start += beat_duration_samples;
                    engine->state.current_beat_start = wrap_position(
                        engine->state.current_beat_start,
                        engine->buffer_size
                    );
                    engine->state.file_position = engine->state.current_beat_start;
                    
                    // Reset all grains to new beat position
                    for (int j = 0; j < GRANULAR_MAX_GRAINS; j++) {
                        engine->state.grains[j].active = false;
                    }
                    start_new_grain(engine, engine->state.current_beat_start);
                } else {
                    // Normal beat sync: reset grain
                    granular_engine_reset_grain(engine);
                }
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
        
        // Spawn new grains based on density (non-beat-sync mode)
        if (!engine->sync_enabled || !engine->params.beat_sync_enabled) {
            engine->grain_spawn_counter += 1.0f;
            
            if (engine->grain_spawn_counter >= grain_spacing) {
                engine->grain_spawn_counter = 0.0f;
                start_new_grain(engine, engine->state.file_position);
            }
        }
        
        // Process all active grains and mix them
        float mixed_sample = 0.0f;
        int active_count = 0;
        
        for (int j = 0; j < GRANULAR_MAX_GRAINS; j++) {
            grain_t *grain = &engine->state.grains[j];
            
            if (!grain->active) continue;
            
            // Check if grain has finished
            if (grain->age >= grain_size_samples) {
                grain->active = false;
                continue;
            }
            
            // Read sample with linear interpolation
            float read_pos = wrap_position(grain->read_pos, engine->buffer_size);
            int32_t idx0 = (int32_t)read_pos;
            int32_t idx1 = (idx0 + 1) % engine->buffer_size;
            float frac = read_pos - idx0;
            
            int16_t sample0 = engine->audio_buffer[idx0];
            int16_t sample1 = engine->audio_buffer[idx1];
            float sample = lerp((float)sample0, (float)sample1, frac);
            
            // Apply window function
            float window_pos = grain->age / grain_size_samples;
            float window = granular_window_value(engine->params.window, window_pos);
            sample *= window;
            
            // Normalize by number of grains to prevent clipping
            // At high density, multiple grains overlap
            mixed_sample += sample;
            active_count++;
            
            // Advance grain read head by pitch factor
            grain->read_pos += engine->params.pitch_factor;
            grain->age += 1.0f;
        }
        
        // Normalize mix (divide by expected grain count, but allow overdrive)
        if (active_count > 0) {
            // Normalize to prevent clipping, but allow density > 100% to create saturation
            float normalization = fminf(1.0f, density_factor);
            mixed_sample = (mixed_sample / (float)active_count) / normalization;
        }
        
        // Clamp and output (stereo)
        int16_t out_sample = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, mixed_sample));
        output[i * 2] = out_sample;     // Left
        output[i * 2 + 1] = out_sample; // Right
        
        engine->grain_counter++;
    }
}

// Add new functions at the end of the file
void granular_engine_set_mode(granular_engine_t *engine, granular_mode_t mode) {
    if (!engine) return;
    engine->mode = mode;
    ESP_LOGI(TAG, "Granular engine mode set to: %s", 
             mode == GRANULAR_MODE_STREAMING ? "streaming" : "multi-grain");
}

size_t granular_engine_write_chunk(granular_engine_t *engine, const int16_t *data, size_t samples) {
    if (!engine || !data || samples == 0) return 0;
    
    // Write samples to circular buffer at write_index
    // data is stereo interleaved (L, R, L, R, ...)
    // buffer stores mono samples, so we'll write left channel only for now
    // TODO: Support stereo if needed
    
    size_t written = 0;
    for (size_t i = 0; i < samples; i++) {
        // Write left channel (every 2nd sample in interleaved data)
        int32_t buffer_idx = engine->write_index % engine->buffer_size;
        engine->audio_buffer[buffer_idx] = data[i * 2]; // Left channel
        
        engine->write_index++;
        written++;
        
        // Wrap around
        if (engine->write_index >= engine->buffer_size) {
            engine->write_index = 0;
        }
    }
    
    return written;
}

int32_t granular_engine_get_write_index(const granular_engine_t *engine) {
    if (!engine) return 0;
    return engine->write_index;
}

int32_t granular_engine_get_read_index(const granular_engine_t *engine) {
    if (!engine) return 0;
    
    if (engine->mode == GRANULAR_MODE_STREAMING) {
        return (int32_t)engine->streaming.read_index;
    } else {
        // In multi-grain mode, use file_position as read index
        return (int32_t)engine->state.file_position;
    }
}

bool granular_engine_check_buffer_distance(const granular_engine_t *engine, 
                                          int32_t loop_limit, 
                                          int32_t refill_threshold) {
    if (!engine) return false;
    
    int32_t write_idx = granular_engine_get_write_index(engine);
    int32_t read_idx = granular_engine_get_read_index(engine);
    
    // Calculate distance (handle wrap-around)
    int32_t distance;
    if (write_idx >= read_idx) {
        distance = write_idx - read_idx;
    } else {
        // Wrap-around case
        distance = (engine->buffer_size - read_idx) + write_idx;
    }
    
    // Check if decoder should pause
    if (distance > loop_limit) {
        return true; // Pause decoder
    }
    
    // Check if decoder should resume
    if (distance < refill_threshold) {
        return false; // Resume decoder
    }
    
    // Keep current state (don't change)
    // Return false to continue/resume
    return false;
}

void granular_engine_set_streaming_params(granular_engine_t *engine, 
                                         float speed, 
                                         float grain_size_samples, 
                                         float jitter) {
    if (!engine) return;
    
    engine->streaming.speed = fmaxf(0.0f, fminf(2.0f, speed));
    engine->streaming.grain_size_samples = fmaxf(100.0f, fminf(10000.0f, grain_size_samples));
    engine->streaming.jitter_amount = fmaxf(0.0f, fminf(1.0f, jitter));
    
    ESP_LOGI(TAG, "Streaming params: speed=%.2f, grain_size=%.0f, jitter=%.2f",
             engine->streaming.speed, engine->streaming.grain_size_samples, engine->streaming.jitter_amount);
}
