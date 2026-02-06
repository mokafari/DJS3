/**
 * @file echo.c
 * @brief Tempo-synced echo/delay effect implementation
 * 
 * Implements a DJ-style echo effect with:
 * - Beat-synced delay times (1/4, 1/2, 3/4, 1 beat)
 * - Feedback with runaway protection (max 95%)
 * - Wet/dry mix control
 * - Circular buffer for efficient delay line
 * 
 * Optimized for ESP32 with IRAM placement for the hot processing loop.
 */

#include "echo.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>

static const char *TAG = "echo";

/** Maximum allowed feedback to prevent runaway oscillation */
#define MAX_FEEDBACK 0.95f

/** Default BPM for initialization */
#define DEFAULT_BPM 120.0f

/** Default feedback amount */
#define DEFAULT_FEEDBACK 0.5f

/** Default wet/dry mix */
#define DEFAULT_MIX 0.5f

/**
 * @brief Beat division multipliers
 * 
 * Maps echo_division_t to fraction of one beat.
 * Index corresponds to enum value.
 */
static const float division_multipliers[ECHO_DIV_MAX] = {
    0.25f,  // ECHO_DIV_QUARTER (1/4 beat)
    0.50f,  // ECHO_DIV_HALF (1/2 beat)
    0.75f,  // ECHO_DIV_THREE_QUARTER (3/4 beat)
    1.00f   // ECHO_DIV_FULL (1 beat)
};

/**
 * @brief Calculate delay in samples for given BPM and division
 * 
 * @param sample_rate Audio sample rate
 * @param bpm Current tempo
 * @param division Beat division
 * @return Delay in stereo sample frames
 */
static size_t calculate_delay_samples(uint32_t sample_rate, float bpm, echo_division_t division)
{
    if (bpm < 30.0f) bpm = 30.0f;   // Minimum 30 BPM
    if (bpm > 300.0f) bpm = 300.0f; // Maximum 300 BPM
    
    // One beat duration in seconds: 60 / BPM
    float beat_duration_sec = 60.0f / bpm;
    
    // Apply division multiplier
    float delay_sec = beat_duration_sec * division_multipliers[division];
    
    // Convert to stereo sample frames
    size_t delay_samples = (size_t)(delay_sec * (float)sample_rate);
    
    return delay_samples;
}

bool echo_init(echo_t *echo, uint32_t sample_rate, uint32_t max_delay_ms)
{
    if (!echo || sample_rate == 0 || max_delay_ms == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    
    // Clear structure
    memset(echo, 0, sizeof(echo_t));
    
    // Calculate buffer size for max delay
    // Buffer stores stereo interleaved samples (2 samples per frame)
    size_t max_frames = (size_t)((float)sample_rate * (float)max_delay_ms / 1000.0f);
    size_t buffer_size = max_frames * 2 * sizeof(int16_t); // Stereo * sample size
    
    // Allocate from SPIRAM if available, otherwise internal RAM
    echo->buffer = (int16_t *)heap_caps_calloc(max_frames * 2, sizeof(int16_t), 
                                                MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!echo->buffer) {
        // Fall back to internal RAM
        echo->buffer = (int16_t *)heap_caps_calloc(max_frames * 2, sizeof(int16_t), 
                                                    MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }
    
    if (!echo->buffer) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for delay buffer", 
                 (unsigned int)buffer_size);
        return false;
    }
    
    echo->buffer_size = max_frames;
    echo->sample_rate = sample_rate;
    echo->write_pos = 0;
    
    // Set defaults
    echo->bpm = DEFAULT_BPM;
    echo->division = ECHO_DIV_QUARTER;
    echo->feedback = DEFAULT_FEEDBACK;
    echo->mix = DEFAULT_MIX;
    echo->enabled = false;
    
    // Calculate initial delay
    echo->delay_samples = calculate_delay_samples(sample_rate, echo->bpm, echo->division);
    if (echo->delay_samples > echo->buffer_size) {
        echo->delay_samples = echo->buffer_size;
    }
    
    ESP_LOGI(TAG, "Echo initialized: %lu Hz, max %lu ms (%u frames), delay %u frames",
             (unsigned long)sample_rate, (unsigned long)max_delay_ms,
             (unsigned int)echo->buffer_size, (unsigned int)echo->delay_samples);
    
    return true;
}

void echo_deinit(echo_t *echo)
{
    if (!echo) return;
    
    if (echo->buffer) {
        heap_caps_free(echo->buffer);
        echo->buffer = NULL;
    }
    
    echo->buffer_size = 0;
    ESP_LOGI(TAG, "Echo deinitialized");
}

void echo_set_bpm(echo_t *echo, float bpm)
{
    if (!echo) return;
    
    echo->bpm = fmaxf(30.0f, fminf(300.0f, bpm));
    echo->delay_samples = calculate_delay_samples(echo->sample_rate, echo->bpm, echo->division);
    
    // Clamp to buffer size
    if (echo->delay_samples > echo->buffer_size) {
        echo->delay_samples = echo->buffer_size;
    }
    
    ESP_LOGD(TAG, "BPM set to %.1f, delay %u samples (%.1f ms)", 
             echo->bpm, (unsigned int)echo->delay_samples,
             echo_get_delay_ms(echo));
}

void echo_set_delay_division(echo_t *echo, echo_division_t division)
{
    if (!echo || division >= ECHO_DIV_MAX) return;
    
    echo->division = division;
    echo->delay_samples = calculate_delay_samples(echo->sample_rate, echo->bpm, echo->division);
    
    // Clamp to buffer size
    if (echo->delay_samples > echo->buffer_size) {
        echo->delay_samples = echo->buffer_size;
    }
    
    ESP_LOGD(TAG, "Division set to %d, delay %u samples", 
             division, (unsigned int)echo->delay_samples);
}

void echo_set_feedback(echo_t *echo, float feedback)
{
    if (!echo) return;
    
    // Clamp feedback to safe range (0 to MAX_FEEDBACK)
    echo->feedback = fmaxf(0.0f, fminf(MAX_FEEDBACK, feedback));
}

void echo_set_mix(echo_t *echo, float mix)
{
    if (!echo) return;
    
    // Clamp mix to valid range
    echo->mix = fmaxf(0.0f, fminf(1.0f, mix));
}

void echo_set_enabled(echo_t *echo, bool enabled)
{
    if (!echo) return;
    echo->enabled = enabled;
}

void echo_clear(echo_t *echo)
{
    if (!echo || !echo->buffer) return;
    
    memset(echo->buffer, 0, echo->buffer_size * 2 * sizeof(int16_t));
    echo->write_pos = 0;
    
    ESP_LOGD(TAG, "Echo buffer cleared");
}

float echo_get_delay_ms(const echo_t *echo)
{
    if (!echo || echo->sample_rate == 0) return 0.0f;
    
    return (float)echo->delay_samples * 1000.0f / (float)echo->sample_rate;
}

/**
 * @brief Soft clip function to prevent harsh digital clipping
 * 
 * Uses polynomial soft clipper: f(x) = x - x³/3
 * 
 * @param x Input value (may exceed -1.0 to 1.0)
 * @return Soft-clipped value
 */
static inline float IRAM_ATTR soft_clip(float x)
{
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x - (x * x * x) / 3.0f;
}

void IRAM_ATTR echo_process(echo_t *echo, int16_t *buffer, size_t samples)
{
    // Early exit conditions
    if (!echo || !echo->enabled || !buffer || samples == 0 || !echo->buffer) {
        return;
    }
    
    // Ensure delay doesn't exceed buffer
    if (echo->delay_samples == 0 || echo->delay_samples > echo->buffer_size) {
        return;
    }
    
    const float scale_in = 1.0f / 32768.0f;
    const float scale_out = 32767.0f;
    const float feedback = echo->feedback;
    const float mix = echo->mix;
    const float dry = 1.0f - mix;
    const size_t delay = echo->delay_samples;
    const size_t buf_size = echo->buffer_size;
    int16_t *delay_buf = echo->buffer;
    size_t write_pos = echo->write_pos;
    
    for (size_t i = 0; i < samples; i++) {
        // Get input samples (stereo)
        float in_l = (float)buffer[i * 2] * scale_in;
        float in_r = (float)buffer[i * 2 + 1] * scale_in;
        
        // Calculate read position (circular buffer)
        size_t read_pos = (write_pos + buf_size - delay) % buf_size;
        
        // Read delayed samples
        float delayed_l = (float)delay_buf[read_pos * 2] * scale_in;
        float delayed_r = (float)delay_buf[read_pos * 2 + 1] * scale_in;
        
        // Calculate wet signal (input + delayed * feedback)
        float wet_l = in_l + delayed_l * feedback;
        float wet_r = in_r + delayed_r * feedback;
        
        // Write to delay buffer (for next iteration's feedback)
        // Soft clip before writing to prevent runaway
        float write_l = soft_clip(wet_l);
        float write_r = soft_clip(wet_r);
        
        delay_buf[write_pos * 2] = (int16_t)(write_l * scale_out);
        delay_buf[write_pos * 2 + 1] = (int16_t)(write_r * scale_out);
        
        // Mix dry and wet signals for output
        float out_l = dry * in_l + mix * delayed_l;
        float out_r = dry * in_r + mix * delayed_r;
        
        // Final soft clip and convert to int16
        out_l = soft_clip(out_l);
        out_r = soft_clip(out_r);
        
        buffer[i * 2] = (int16_t)(out_l * scale_out);
        buffer[i * 2 + 1] = (int16_t)(out_r * scale_out);
        
        // Advance write position
        write_pos = (write_pos + 1) % buf_size;
    }
    
    // Save write position for next call
    echo->write_pos = write_pos;
}
