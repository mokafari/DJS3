/**
 * @file dsp_pipeline.c
 * @brief DSP effect pipeline implementation
 * 
 * Provides chainable audio effects with thread-safe parameter updates.
 * Optimized for ESP32-S3 with fixed-point math where appropriate.
 */

#include "dsp_pipeline.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <math.h>

static const char *TAG = "dsp_pipeline";

// Math constants
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Clamp helper
static inline float clampf(float x, float min, float max) {
    return x < min ? min : (x > max ? max : x);
}

// Convert int16 to float (-1.0 to 1.0)
static inline float sample_to_float(int16_t s) {
    return (float)s / 32768.0f;
}

// Convert float to int16 with saturation
static inline int16_t float_to_sample(float f) {
    f = clampf(f, -1.0f, 1.0f);
    return (int16_t)(f * 32767.0f);
}

// ============================================================================
// Filter Coefficient Calculation (Biquad)
// ============================================================================

static void calc_biquad_lpf(dsp_filter_state_t *state, float fc, float q, float fs) {
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    state->b0 = ((1.0f - cos_w0) / 2.0f) / a0;
    state->b1 = (1.0f - cos_w0) / a0;
    state->b2 = state->b0;
    state->a1 = (-2.0f * cos_w0) / a0;
    state->a2 = (1.0f - alpha) / a0;
}

static void calc_biquad_hpf(dsp_filter_state_t *state, float fc, float q, float fs) {
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    state->b0 = ((1.0f + cos_w0) / 2.0f) / a0;
    state->b1 = (-(1.0f + cos_w0)) / a0;
    state->b2 = state->b0;
    state->a1 = (-2.0f * cos_w0) / a0;
    state->a2 = (1.0f - alpha) / a0;
}

static void calc_biquad_bpf(dsp_filter_state_t *state, float fc, float q, float fs) {
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    state->b0 = alpha / a0;
    state->b1 = 0.0f;
    state->b2 = -alpha / a0;
    state->a1 = (-2.0f * cos_w0) / a0;
    state->a2 = (1.0f - alpha) / a0;
}

static void calc_biquad_peak(dsp_filter_state_t *state, float fc, float gain, float q, float fs) {
    float A = sqrtf(gain);
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * q);
    
    float a0 = 1.0f + alpha / A;
    state->b0 = (1.0f + alpha * A) / a0;
    state->b1 = (-2.0f * cos_w0) / a0;
    state->b2 = (1.0f - alpha * A) / a0;
    state->a1 = (-2.0f * cos_w0) / a0;
    state->a2 = (1.0f - alpha / A) / a0;
}

static void calc_biquad_lowshelf(dsp_filter_state_t *state, float fc, float gain, float fs) {
    float A = sqrtf(gain);
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / 2.0f * sqrtf(2.0f);
    
    float a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha;
    state->b0 = (A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha)) / a0;
    state->b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0)) / a0;
    state->b2 = (A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha)) / a0;
    state->a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0)) / a0;
    state->a2 = ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha) / a0;
}

static void calc_biquad_highshelf(dsp_filter_state_t *state, float fc, float gain, float fs) {
    float A = sqrtf(gain);
    float w0 = 2.0f * M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / 2.0f * sqrtf(2.0f);
    
    float a0 = (A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha;
    state->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha)) / a0;
    state->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0)) / a0;
    state->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha)) / a0;
    state->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0)) / a0;
    state->a2 = ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha) / a0;
}

static void reset_filter_state(dsp_filter_state_t *state) {
    state->x1_l = state->x2_l = 0.0f;
    state->y1_l = state->y2_l = 0.0f;
    state->x1_r = state->x2_r = 0.0f;
    state->y1_r = state->y2_r = 0.0f;
}

// ============================================================================
// Biquad Processing
// ============================================================================

static inline float process_biquad_sample(dsp_filter_state_t *state, float in, 
                                           float *x1, float *x2, float *y1, float *y2) {
    float out = state->b0 * in + state->b1 * (*x1) + state->b2 * (*x2)
                - state->a1 * (*y1) - state->a2 * (*y2);
    *x2 = *x1;
    *x1 = in;
    *y2 = *y1;
    *y1 = out;
    return out;
}

static void process_filter(dsp_filter_state_t *state, float *left, float *right, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        left[i] = process_biquad_sample(state, left[i], 
                                        &state->x1_l, &state->x2_l, 
                                        &state->y1_l, &state->y2_l);
        right[i] = process_biquad_sample(state, right[i],
                                         &state->x1_r, &state->x2_r,
                                         &state->y1_r, &state->y2_r);
    }
}

// ============================================================================
// Effect Processing Functions
// ============================================================================

static void process_effect_filter(dsp_effect_slot_t *slot, float *left, float *right, 
                                   size_t frames, uint32_t sample_rate) {
    dsp_filter_params_t *params = &slot->config.params.filter;
    dsp_filter_state_t *state = &slot->state.filter;
    
    // Recalculate coefficients (could optimize by caching)
    float cutoff = clampf(params->cutoff_hz, 20.0f, sample_rate / 2.0f - 100.0f);
    float q = clampf(params->resonance, 0.5f, 10.0f);
    
    switch (params->mode) {
        case DSP_FILTER_LOWPASS:
            calc_biquad_lpf(state, cutoff, q, (float)sample_rate);
            break;
        case DSP_FILTER_HIGHPASS:
            calc_biquad_hpf(state, cutoff, q, (float)sample_rate);
            break;
        case DSP_FILTER_BANDPASS:
            calc_biquad_bpf(state, cutoff, q, (float)sample_rate);
            break;
    }
    
    process_filter(state, left, right, frames);
}

static void process_effect_eq(dsp_effect_slot_t *slot, float *left, float *right,
                               size_t frames, uint32_t sample_rate) {
    dsp_eq_params_t *params = &slot->config.params.eq;
    dsp_eq_state_t *state = &slot->state.eq;
    
    // Calculate shelf and peak EQ coefficients
    calc_biquad_lowshelf(&state->low, params->low_freq, params->low_gain, (float)sample_rate);
    calc_biquad_peak(&state->mid, sqrtf(params->low_freq * params->high_freq), 
                     params->mid_gain, 1.0f, (float)sample_rate);
    calc_biquad_highshelf(&state->high, params->high_freq, params->high_gain, (float)sample_rate);
    
    // Process through all three bands
    process_filter(&state->low, left, right, frames);
    process_filter(&state->mid, left, right, frames);
    process_filter(&state->high, left, right, frames);
}

static void process_effect_echo(dsp_effect_slot_t *slot, float *left, float *right,
                                 size_t frames, uint32_t sample_rate) {
    dsp_echo_params_t *params = &slot->config.params.echo;
    dsp_echo_state_t *state = &slot->state.echo;
    
    if (!state->buffer_l || !state->buffer_r) {
        return;  // Echo buffers not allocated
    }
    
    size_t delay_samples = (size_t)(params->delay_ms * (float)sample_rate / 1000.0f);
    if (delay_samples >= state->buffer_size) {
        delay_samples = state->buffer_size - 1;
    }
    
    float feedback = clampf(params->feedback, 0.0f, 0.95f);
    float wet = clampf(params->wet_mix, 0.0f, 1.0f);
    float dry = 1.0f - wet;
    
    for (size_t i = 0; i < frames; i++) {
        // Read from delay buffer
        size_t read_pos = (state->write_pos + state->buffer_size - delay_samples) % state->buffer_size;
        
        float delayed_l = sample_to_float(state->buffer_l[read_pos]);
        float delayed_r = sample_to_float(state->buffer_r[read_pos]);
        
        // Write to delay buffer (input + feedback)
        state->buffer_l[state->write_pos] = float_to_sample(left[i] + delayed_l * feedback);
        state->buffer_r[state->write_pos] = float_to_sample(right[i] + delayed_r * feedback);
        
        // Mix dry and wet
        left[i] = left[i] * dry + delayed_l * wet;
        right[i] = right[i] * dry + delayed_r * wet;
        
        // Advance write position
        state->write_pos = (state->write_pos + 1) % state->buffer_size;
    }
}

static void process_effect_limiter(dsp_effect_slot_t *slot, float *left, float *right,
                                    size_t frames, uint32_t sample_rate) {
    dsp_limiter_params_t *params = &slot->config.params.limiter;
    dsp_limiter_state_t *state = &slot->state.limiter;
    
    float threshold = clampf(params->threshold, 0.1f, 1.0f);
    float ceiling = clampf(params->ceiling, 0.1f, 1.0f);
    
    // Attack is instant, release is smooth
    float release_coef = expf(-1.0f / (params->release_ms * (float)sample_rate / 1000.0f));
    
    for (size_t i = 0; i < frames; i++) {
        // Peak detection (max of L/R)
        float peak = fmaxf(fabsf(left[i]), fabsf(right[i]));
        
        // Envelope follower
        if (peak > state->envelope) {
            state->envelope = peak;  // Instant attack
        } else {
            state->envelope = state->envelope * release_coef + peak * (1.0f - release_coef);
        }
        
        // Calculate gain reduction
        if (state->envelope > threshold) {
            state->gain_reduction = threshold / state->envelope;
        } else {
            state->gain_reduction = 1.0f;
        }
        
        // Apply gain and ceiling
        float gain = state->gain_reduction * ceiling;
        left[i] *= gain;
        right[i] *= gain;
        
        // Hard clip at ceiling (safety)
        left[i] = clampf(left[i], -ceiling, ceiling);
        right[i] = clampf(right[i], -ceiling, ceiling);
    }
}

// ============================================================================
// Pipeline Lifecycle
// ============================================================================

bool dsp_pipeline_init(dsp_pipeline_t *pipeline, uint32_t sample_rate) {
    if (!pipeline) return false;
    
    memset(pipeline, 0, sizeof(dsp_pipeline_t));
    
    pipeline->mutex = xSemaphoreCreateMutex();
    if (!pipeline->mutex) {
        ESP_LOGE(TAG, "Failed to create pipeline mutex");
        return false;
    }
    
    pipeline->sample_rate = sample_rate;
    
    // Allocate work buffers for real-time safe processing (no malloc in audio path)
    pipeline->work_buffer_frames = DSP_MAX_FRAMES_PER_CALL;
    pipeline->work_buffer_l = heap_caps_malloc(DSP_MAX_FRAMES_PER_CALL * sizeof(float),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pipeline->work_buffer_r = heap_caps_malloc(DSP_MAX_FRAMES_PER_CALL * sizeof(float),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    // Fallback to internal RAM if SPIRAM not available
    if (!pipeline->work_buffer_l || !pipeline->work_buffer_r) {
        if (pipeline->work_buffer_l) heap_caps_free(pipeline->work_buffer_l);
        if (pipeline->work_buffer_r) heap_caps_free(pipeline->work_buffer_r);
        
        pipeline->work_buffer_l = malloc(DSP_MAX_FRAMES_PER_CALL * sizeof(float));
        pipeline->work_buffer_r = malloc(DSP_MAX_FRAMES_PER_CALL * sizeof(float));
        
        if (!pipeline->work_buffer_l || !pipeline->work_buffer_r) {
            ESP_LOGE(TAG, "Failed to allocate work buffers");
            if (pipeline->work_buffer_l) free(pipeline->work_buffer_l);
            if (pipeline->work_buffer_r) free(pipeline->work_buffer_r);
            vSemaphoreDelete(pipeline->mutex);
            return false;
        }
    }
    
    // Configure default master limiter
    pipeline->master_limiter.enabled = true;
    pipeline->master_limiter.threshold = 0.95f;
    pipeline->master_limiter.ceiling = 0.99f;
    pipeline->master_limiter.release_ms = 50.0f;
    
    pipeline->master_limiter_state.envelope = 0.0f;
    pipeline->master_limiter_state.gain_reduction = 1.0f;
    
    pipeline->initialized = true;
    
    ESP_LOGI(TAG, "DSP pipeline initialized (sample rate: %lu Hz)", (unsigned long)sample_rate);
    return true;
}

void dsp_pipeline_destroy(dsp_pipeline_t *pipeline) {
    if (!pipeline || !pipeline->initialized) return;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    // Free echo buffers
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        if (pipeline->effects[i].active && 
            pipeline->effects[i].config.type == DSP_EFFECT_ECHO) {
            dsp_echo_state_t *echo = &pipeline->effects[i].state.echo;
            if (echo->buffer_l) {
                heap_caps_free(echo->buffer_l);
                echo->buffer_l = NULL;
            }
            if (echo->buffer_r) {
                heap_caps_free(echo->buffer_r);
                echo->buffer_r = NULL;
            }
        }
    }
    
    // Free pre-allocated work buffers
    if (pipeline->work_buffer_l) {
        free(pipeline->work_buffer_l);
        pipeline->work_buffer_l = NULL;
    }
    if (pipeline->work_buffer_r) {
        free(pipeline->work_buffer_r);
        pipeline->work_buffer_r = NULL;
    }
    
    xSemaphoreGive(pipeline->mutex);
    vSemaphoreDelete(pipeline->mutex);
    
    pipeline->initialized = false;
    ESP_LOGI(TAG, "DSP pipeline destroyed");
}

void dsp_pipeline_reset(dsp_pipeline_t *pipeline) {
    if (!pipeline || !pipeline->initialized) return;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        if (!pipeline->effects[i].active) continue;
        
        switch (pipeline->effects[i].config.type) {
            case DSP_EFFECT_FILTER:
                reset_filter_state(&pipeline->effects[i].state.filter);
                break;
            case DSP_EFFECT_EQ:
                reset_filter_state(&pipeline->effects[i].state.eq.low);
                reset_filter_state(&pipeline->effects[i].state.eq.mid);
                reset_filter_state(&pipeline->effects[i].state.eq.high);
                break;
            case DSP_EFFECT_ECHO:
                if (pipeline->effects[i].state.echo.buffer_l) {
                    memset(pipeline->effects[i].state.echo.buffer_l, 0, 
                           pipeline->effects[i].state.echo.buffer_size * sizeof(int16_t));
                }
                if (pipeline->effects[i].state.echo.buffer_r) {
                    memset(pipeline->effects[i].state.echo.buffer_r, 0,
                           pipeline->effects[i].state.echo.buffer_size * sizeof(int16_t));
                }
                pipeline->effects[i].state.echo.write_pos = 0;
                break;
            case DSP_EFFECT_LIMITER:
                pipeline->effects[i].state.limiter.envelope = 0.0f;
                pipeline->effects[i].state.limiter.gain_reduction = 1.0f;
                break;
            default:
                break;
        }
    }
    
    // Reset master limiter state
    pipeline->master_limiter_state.envelope = 0.0f;
    pipeline->master_limiter_state.gain_reduction = 1.0f;
    
    xSemaphoreGive(pipeline->mutex);
}

// ============================================================================
// Effect Management
// ============================================================================

int dsp_pipeline_add_effect(dsp_pipeline_t *pipeline, const dsp_effect_config_t *config) {
    if (!pipeline || !pipeline->initialized || !config) return -1;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    // Find empty slot
    int slot_index = -1;
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        if (!pipeline->effects[i].active) {
            slot_index = i;
            break;
        }
    }
    
    if (slot_index < 0) {
        ESP_LOGW(TAG, "No empty effect slots available");
        xSemaphoreGive(pipeline->mutex);
        return -1;
    }
    
    dsp_effect_slot_t *slot = &pipeline->effects[slot_index];
    memset(slot, 0, sizeof(dsp_effect_slot_t));
    slot->config = *config;
    slot->active = true;
    
    // Initialize effect-specific state
    switch (config->type) {
        case DSP_EFFECT_FILTER:
            reset_filter_state(&slot->state.filter);
            break;
            
        case DSP_EFFECT_EQ:
            reset_filter_state(&slot->state.eq.low);
            reset_filter_state(&slot->state.eq.mid);
            reset_filter_state(&slot->state.eq.high);
            break;
            
        case DSP_EFFECT_ECHO: {
            // Allocate echo buffers in SPIRAM if available
            size_t buffer_size = DSP_ECHO_BUFFER_SAMPLES;
            slot->state.echo.buffer_l = heap_caps_calloc(buffer_size, sizeof(int16_t),
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            slot->state.echo.buffer_r = heap_caps_calloc(buffer_size, sizeof(int16_t),
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            
            if (!slot->state.echo.buffer_l || !slot->state.echo.buffer_r) {
                // Try internal RAM if SPIRAM failed
                if (slot->state.echo.buffer_l) heap_caps_free(slot->state.echo.buffer_l);
                if (slot->state.echo.buffer_r) heap_caps_free(slot->state.echo.buffer_r);
                
                slot->state.echo.buffer_l = calloc(buffer_size, sizeof(int16_t));
                slot->state.echo.buffer_r = calloc(buffer_size, sizeof(int16_t));
                
                if (!slot->state.echo.buffer_l || !slot->state.echo.buffer_r) {
                    ESP_LOGE(TAG, "Failed to allocate echo buffers");
                    slot->active = false;
                    xSemaphoreGive(pipeline->mutex);
                    return -1;
                }
            }
            slot->state.echo.buffer_size = buffer_size;
            slot->state.echo.write_pos = 0;
            ESP_LOGI(TAG, "Echo buffer allocated: %zu samples per channel", buffer_size);
            break;
        }
        
        case DSP_EFFECT_LIMITER:
            slot->state.limiter.envelope = 0.0f;
            slot->state.limiter.gain_reduction = 1.0f;
            break;
            
        default:
            break;
    }
    
    pipeline->effect_count++;
    
    ESP_LOGI(TAG, "Added effect type %d to slot %d", config->type, slot_index);
    xSemaphoreGive(pipeline->mutex);
    return slot_index;
}

bool dsp_pipeline_remove_effect(dsp_pipeline_t *pipeline, int slot_index) {
    if (!pipeline || !pipeline->initialized) return false;
    if (slot_index < 0 || slot_index >= DSP_MAX_EFFECTS) return false;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    dsp_effect_slot_t *slot = &pipeline->effects[slot_index];
    if (!slot->active) {
        xSemaphoreGive(pipeline->mutex);
        return false;
    }
    
    // Free effect-specific resources
    if (slot->config.type == DSP_EFFECT_ECHO) {
        if (slot->state.echo.buffer_l) {
            heap_caps_free(slot->state.echo.buffer_l);
            slot->state.echo.buffer_l = NULL;
        }
        if (slot->state.echo.buffer_r) {
            heap_caps_free(slot->state.echo.buffer_r);
            slot->state.echo.buffer_r = NULL;
        }
    }
    
    slot->active = false;
    pipeline->effect_count--;
    
    ESP_LOGI(TAG, "Removed effect from slot %d", slot_index);
    xSemaphoreGive(pipeline->mutex);
    return true;
}

bool dsp_pipeline_update_effect(dsp_pipeline_t *pipeline, int slot_index,
                                 const dsp_effect_config_t *config) {
    if (!pipeline || !pipeline->initialized || !config) return false;
    if (slot_index < 0 || slot_index >= DSP_MAX_EFFECTS) return false;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    dsp_effect_slot_t *slot = &pipeline->effects[slot_index];
    if (!slot->active || slot->config.type != config->type) {
        xSemaphoreGive(pipeline->mutex);
        return false;
    }
    
    // Update configuration (atomic from processing perspective)
    slot->config = *config;
    
    xSemaphoreGive(pipeline->mutex);
    return true;
}

bool dsp_pipeline_set_bypass(dsp_pipeline_t *pipeline, int slot_index, bool bypass) {
    if (!pipeline || !pipeline->initialized) return false;
    if (slot_index < 0 || slot_index >= DSP_MAX_EFFECTS) return false;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    if (pipeline->effects[slot_index].active) {
        pipeline->effects[slot_index].config.bypass = bypass;
        xSemaphoreGive(pipeline->mutex);
        return true;
    }
    
    xSemaphoreGive(pipeline->mutex);
    return false;
}

bool dsp_pipeline_get_effect(dsp_pipeline_t *pipeline, int slot_index,
                              dsp_effect_config_t *config) {
    if (!pipeline || !pipeline->initialized || !config) return false;
    if (slot_index < 0 || slot_index >= DSP_MAX_EFFECTS) return false;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    if (pipeline->effects[slot_index].active) {
        *config = pipeline->effects[slot_index].config;
        xSemaphoreGive(pipeline->mutex);
        return true;
    }
    
    xSemaphoreGive(pipeline->mutex);
    return false;
}

// ============================================================================
// Master Limiter
// ============================================================================

void dsp_pipeline_set_master_limiter(dsp_pipeline_t *pipeline,
                                      const dsp_master_limiter_t *config) {
    if (!pipeline || !pipeline->initialized || !config) return;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    pipeline->master_limiter = *config;
    xSemaphoreGive(pipeline->mutex);
}

void dsp_pipeline_enable_master_limiter(dsp_pipeline_t *pipeline, bool enabled) {
    if (!pipeline || !pipeline->initialized) return;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    pipeline->master_limiter.enabled = enabled;
    xSemaphoreGive(pipeline->mutex);
}

// ============================================================================
// Audio Processing
// ============================================================================

static void process_master_limiter(dsp_pipeline_t *pipeline, float *left, float *right,
                                    size_t frames) {
    dsp_master_limiter_t *params = &pipeline->master_limiter;
    dsp_limiter_state_t *state = &pipeline->master_limiter_state;
    
    float threshold = params->threshold;
    float ceiling = params->ceiling;
    float release_coef = expf(-1.0f / (params->release_ms * (float)pipeline->sample_rate / 1000.0f));
    
    for (size_t i = 0; i < frames; i++) {
        float peak = fmaxf(fabsf(left[i]), fabsf(right[i]));
        
        if (peak > state->envelope) {
            state->envelope = peak;
        } else {
            state->envelope = state->envelope * release_coef + peak * (1.0f - release_coef);
        }
        
        if (state->envelope > threshold) {
            state->gain_reduction = threshold / state->envelope;
        } else {
            state->gain_reduction = 1.0f;
        }
        
        float gain = state->gain_reduction * ceiling;
        left[i] = clampf(left[i] * gain, -ceiling, ceiling);
        right[i] = clampf(right[i] * gain, -ceiling, ceiling);
    }
}

void dsp_pipeline_process(dsp_pipeline_t *pipeline, int16_t *samples, size_t num_frames) {
    if (!pipeline || !pipeline->initialized || !samples || num_frames == 0) return;
    
    // Use pre-allocated buffers - real-time safe (no malloc in audio path)
    float *left = pipeline->work_buffer_l;
    float *right = pipeline->work_buffer_r;
    
    // Process in chunks if num_frames exceeds buffer size
    size_t frames_processed = 0;
    while (frames_processed < num_frames) {
        size_t chunk_frames = num_frames - frames_processed;
        if (chunk_frames > pipeline->work_buffer_frames) {
            chunk_frames = pipeline->work_buffer_frames;
        }
        
        int16_t *chunk_samples = samples + (frames_processed * 2);
        
        // Deinterleave and convert to float
        for (size_t i = 0; i < chunk_frames; i++) {
            left[i] = sample_to_float(chunk_samples[i * 2]);
            right[i] = sample_to_float(chunk_samples[i * 2 + 1]);
        }
        
        // Process float version
        dsp_pipeline_process_float(pipeline, left, right, chunk_frames);
        
        // Convert back and interleave
        for (size_t i = 0; i < chunk_frames; i++) {
            chunk_samples[i * 2] = float_to_sample(left[i]);
            chunk_samples[i * 2 + 1] = float_to_sample(right[i]);
        }
        
        frames_processed += chunk_frames;
    }
}

void dsp_pipeline_process_float(dsp_pipeline_t *pipeline,
                                 float *samples_l, float *samples_r,
                                 size_t num_frames) {
    if (!pipeline || !pipeline->initialized) return;
    if (!samples_l || !samples_r || num_frames == 0) return;
    
    xSemaphoreTake(pipeline->mutex, portMAX_DELAY);
    
    // Process effects in fixed order: Filter → EQ → Echo → Limiter
    // This ensures consistent signal flow regardless of slot assignment
    
    // Stage 1: Process all FILTER effects (bypassed ones are skipped)
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        dsp_effect_slot_t *slot = &pipeline->effects[i];
        if (slot->active && !slot->config.bypass && slot->config.type == DSP_EFFECT_FILTER) {
            process_effect_filter(slot, samples_l, samples_r, num_frames, pipeline->sample_rate);
        }
    }
    
    // Stage 2: Process all EQ effects
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        dsp_effect_slot_t *slot = &pipeline->effects[i];
        if (slot->active && !slot->config.bypass && slot->config.type == DSP_EFFECT_EQ) {
            process_effect_eq(slot, samples_l, samples_r, num_frames, pipeline->sample_rate);
        }
    }
    
    // Stage 3: Process all ECHO effects
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        dsp_effect_slot_t *slot = &pipeline->effects[i];
        if (slot->active && !slot->config.bypass && slot->config.type == DSP_EFFECT_ECHO) {
            process_effect_echo(slot, samples_l, samples_r, num_frames, pipeline->sample_rate);
        }
    }
    
    // Stage 4: Process per-slot LIMITER effects (before master limiter)
    for (int i = 0; i < DSP_MAX_EFFECTS; i++) {
        dsp_effect_slot_t *slot = &pipeline->effects[i];
        if (slot->active && !slot->config.bypass && slot->config.type == DSP_EFFECT_LIMITER) {
            process_effect_limiter(slot, samples_l, samples_r, num_frames, pipeline->sample_rate);
        }
    }
    
    // Stage 5: Apply master limiter at end of chain (prevents clipping)
    if (pipeline->master_limiter.enabled) {
        process_master_limiter(pipeline, samples_l, samples_r, num_frames);
    }
    
    xSemaphoreGive(pipeline->mutex);
}

// ============================================================================
// Preset Configurations
// ============================================================================

void dsp_filter_default(dsp_effect_config_t *config, dsp_filter_mode_t mode, float cutoff_hz) {
    if (!config) return;
    
    memset(config, 0, sizeof(dsp_effect_config_t));
    config->type = DSP_EFFECT_FILTER;
    config->bypass = false;
    config->params.filter.mode = mode;
    config->params.filter.cutoff_hz = cutoff_hz;
    config->params.filter.resonance = 0.707f;  // Butterworth Q
}

void dsp_eq_default(dsp_effect_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(dsp_effect_config_t));
    config->type = DSP_EFFECT_EQ;
    config->bypass = false;
    config->params.eq.low_gain = 1.0f;
    config->params.eq.mid_gain = 1.0f;
    config->params.eq.high_gain = 1.0f;
    config->params.eq.low_freq = 200.0f;
    config->params.eq.high_freq = 3000.0f;
}

void dsp_echo_default(dsp_effect_config_t *config, float delay_ms) {
    if (!config) return;
    
    memset(config, 0, sizeof(dsp_effect_config_t));
    config->type = DSP_EFFECT_ECHO;
    config->bypass = false;
    config->params.echo.delay_ms = delay_ms;
    config->params.echo.feedback = 0.3f;
    config->params.echo.wet_mix = 0.25f;
}

void dsp_limiter_default(dsp_effect_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(dsp_effect_config_t));
    config->type = DSP_EFFECT_LIMITER;
    config->bypass = false;
    config->params.limiter.threshold = 0.95f;
    config->params.limiter.release_ms = 50.0f;
    config->params.limiter.ceiling = 0.99f;
}
