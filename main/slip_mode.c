/**
 * @file slip_mode.c
 * @brief Slip Mode Implementation - Background timeline for non-destructive scratching/looping
 * 
 * The background timeline works by:
 * 1. When a slip begins, we capture the current position and system time
 * 2. While slipping, background_position = slip_start_position + (elapsed_time * speed * bytes_per_ms)
 * 3. When slip ends, we return the background position for seeking
 * 
 * The beauty of slip mode is that it's passive - it doesn't interfere with
 * normal playback, it just tracks where playback *should* be.
 */

#include "slip_mode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "slip_mode";

// Default crossfade duration (ms) - 0 for instant snap (DJ preference)
#define DEFAULT_CROSSFADE_MS 0

// Bytes per millisecond at 44.1kHz stereo 16-bit = 44100 * 4 / 1000 = 176.4
#define BYTES_PER_MS_44100 176.4f

// ============================================================================
// Internal State
// ============================================================================

static slip_mode_state_t slip_state = {0};
static SemaphoreHandle_t slip_mutex = NULL;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief Calculate bytes per millisecond based on sample rate
 */
static float get_bytes_per_ms(void) {
    // 16-bit stereo = 4 bytes per sample frame
    return (float)slip_state.sample_rate * 4.0f / 1000.0f;
}

/**
 * @brief Calculate background position based on elapsed time
 */
static uint64_t calculate_background_position(void) {
    if (!slip_state.active) {
        return slip_state.background_position_bytes;
    }
    
    uint32_t now_ms = get_time_ms();
    uint32_t elapsed_ms = now_ms - slip_state.background_start_time_ms;
    
    // Calculate how many bytes would have been played
    float bytes_per_ms = get_bytes_per_ms();
    float bytes_elapsed = (float)elapsed_ms * bytes_per_ms * slip_state.speed_ratio;
    
    return slip_state.slip_start_position_bytes + (uint64_t)bytes_elapsed;
}

// ============================================================================
// Core API Implementation
// ============================================================================

void slip_mode_init(void) {
    ESP_LOGI(TAG, "Initializing slip mode system");
    
    // Create mutex for thread safety
    if (slip_mutex == NULL) {
        slip_mutex = xSemaphoreCreateMutex();
    }
    
    // Initialize state
    memset(&slip_state, 0, sizeof(slip_mode_state_t));
    slip_state.enabled = false;
    slip_state.active = false;
    slip_state.trigger = SLIP_TRIGGER_NONE;
    slip_state.speed_ratio = 1.0f;
    slip_state.sample_rate = 44100;
    slip_state.snap_crossfade_ms = DEFAULT_CROSSFADE_MS;
    slip_state.snap_pending = false;
    slip_state.snap_progress = 0.0f;
    
    ESP_LOGI(TAG, "Slip mode initialized (crossfade: %u ms)", slip_state.snap_crossfade_ms);
}

void slip_mode_enable(void) {
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    if (!slip_state.enabled) {
        slip_state.enabled = true;
        ESP_LOGI(TAG, "Slip mode ENABLED");
    }
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
}

void slip_mode_disable(void) {
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    if (slip_state.enabled) {
        // If currently slipping, end without snap
        if (slip_state.active) {
            slip_state.active = false;
            slip_state.trigger = SLIP_TRIGGER_NONE;
            slip_state.snap_pending = false;
            ESP_LOGW(TAG, "Slip mode disabled while active - cancelled slip");
        }
        
        slip_state.enabled = false;
        ESP_LOGI(TAG, "Slip mode DISABLED");
    }
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
}

bool slip_mode_toggle(void) {
    bool new_state;
    
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    if (slip_state.enabled) {
        // Disable - cancel any active slip
        if (slip_state.active) {
            slip_state.active = false;
            slip_state.trigger = SLIP_TRIGGER_NONE;
            slip_state.snap_pending = false;
        }
        slip_state.enabled = false;
        new_state = false;
    } else {
        slip_state.enabled = true;
        new_state = true;
    }
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
    
    ESP_LOGI(TAG, "Slip mode %s", new_state ? "ENABLED" : "DISABLED");
    return new_state;
}

bool slip_mode_is_enabled(void) {
    return slip_state.enabled;
}

// ============================================================================
// Slip Operations Implementation
// ============================================================================

void slip_mode_begin_slip(slip_trigger_t trigger, uint64_t current_position_bytes) {
    if (!slip_state.enabled) {
        return;  // Slip mode not enabled, ignore
    }
    
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    if (!slip_state.active) {
        slip_state.active = true;
        slip_state.trigger = trigger;
        slip_state.slip_start_position_bytes = current_position_bytes;
        slip_state.background_position_bytes = current_position_bytes;
        slip_state.background_start_time_ms = get_time_ms();
        slip_state.snap_pending = false;
        slip_state.snap_progress = 0.0f;
        
        const char* trigger_name = "unknown";
        switch (trigger) {
            case SLIP_TRIGGER_SCRATCH: trigger_name = "SCRATCH"; break;
            case SLIP_TRIGGER_LOOP: trigger_name = "LOOP"; break;
            case SLIP_TRIGGER_PAUSE: trigger_name = "PAUSE"; break;
            case SLIP_TRIGGER_HOT_CUE: trigger_name = "HOT_CUE"; break;
            case SLIP_TRIGGER_REVERSE: trigger_name = "REVERSE"; break;
            default: break;
        }
        
        ESP_LOGI(TAG, "Slip STARTED: trigger=%s position=%llu bytes", 
                 trigger_name, current_position_bytes);
    } else {
        ESP_LOGD(TAG, "Slip already active, ignoring begin_slip");
    }
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
}

uint64_t slip_mode_end_slip(void) {
    uint64_t target_position = 0;
    
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    if (slip_state.active) {
        // Calculate where background timeline has advanced to
        target_position = calculate_background_position();
        
        uint32_t elapsed_ms = get_time_ms() - slip_state.background_start_time_ms;
        
        ESP_LOGI(TAG, "Slip ENDED: elapsed=%u ms, snap to %llu bytes (delta: %lld bytes)",
                 elapsed_ms, 
                 target_position,
                 (int64_t)(target_position - slip_state.slip_start_position_bytes));
        
        // Set up snap transition
        if (slip_state.snap_crossfade_ms > 0) {
            slip_state.snap_pending = true;
            slip_state.snap_target_bytes = target_position;
            slip_state.snap_progress = 0.0f;
        }
        
        // Update background position for reference
        slip_state.background_position_bytes = target_position;
        
        // End the slip
        slip_state.active = false;
        slip_state.trigger = SLIP_TRIGGER_NONE;
    }
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
    
    return target_position;
}

bool slip_mode_is_active(void) {
    return slip_state.active;
}

slip_trigger_t slip_mode_get_trigger(void) {
    return slip_state.trigger;
}

// ============================================================================
// Position Tracking Implementation
// ============================================================================

void slip_mode_update(void) {
    if (!slip_state.enabled || !slip_state.active) {
        return;  // Nothing to update
    }
    
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    // Recalculate background position based on elapsed time
    slip_state.background_position_bytes = calculate_background_position();
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
}

void slip_mode_set_speed(float speed_ratio) {
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    // If currently slipping and speed changes, we need to recalculate
    if (slip_state.active && speed_ratio != slip_state.speed_ratio) {
        // Save current calculated position before speed change
        uint64_t current_bg_pos = calculate_background_position();
        
        // Update state for new speed
        slip_state.slip_start_position_bytes = current_bg_pos;
        slip_state.background_start_time_ms = get_time_ms();
        
        ESP_LOGD(TAG, "Speed changed during slip: %.2f -> %.2f, rebased at %llu",
                 slip_state.speed_ratio, speed_ratio, current_bg_pos);
    }
    
    slip_state.speed_ratio = speed_ratio;
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
}

void slip_mode_set_sample_rate(uint32_t sample_rate) {
    if (sample_rate > 0) {
        slip_state.sample_rate = sample_rate;
    }
}

uint64_t slip_mode_get_background_position(void) {
    if (slip_state.active) {
        return calculate_background_position();
    }
    return slip_state.background_position_bytes;
}

void slip_mode_sync_position(uint64_t position_bytes) {
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    // End any active slip when position is explicitly synced
    if (slip_state.active) {
        ESP_LOGW(TAG, "Position sync while slip active - cancelling slip");
        slip_state.active = false;
        slip_state.trigger = SLIP_TRIGGER_NONE;
        slip_state.snap_pending = false;
    }
    
    slip_state.background_position_bytes = position_bytes;
    slip_state.slip_start_position_bytes = position_bytes;
    slip_state.background_start_time_ms = get_time_ms();
    
    ESP_LOGD(TAG, "Position synced to %llu bytes", position_bytes);
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
}

// ============================================================================
// Transition Control Implementation
// ============================================================================

void slip_mode_set_crossfade(uint32_t duration_ms) {
    slip_state.snap_crossfade_ms = duration_ms;
    ESP_LOGI(TAG, "Crossfade duration set to %u ms", duration_ms);
}

bool slip_mode_is_snapping(void) {
    return slip_state.snap_pending;
}

float slip_mode_get_snap_progress(void) {
    return slip_state.snap_progress;
}

uint64_t slip_mode_process_snap(uint64_t current_position_bytes, uint32_t delta_time_ms) {
    if (!slip_state.snap_pending) {
        return current_position_bytes;
    }
    
    if (slip_mutex) xSemaphoreTake(slip_mutex, portMAX_DELAY);
    
    uint64_t result_position;
    
    if (slip_state.snap_crossfade_ms == 0) {
        // Instant snap
        result_position = slip_state.snap_target_bytes;
        slip_state.snap_pending = false;
        slip_state.snap_progress = 1.0f;
    } else {
        // Calculate progress
        float progress_delta = (float)delta_time_ms / (float)slip_state.snap_crossfade_ms;
        slip_state.snap_progress += progress_delta;
        
        if (slip_state.snap_progress >= 1.0f) {
            // Snap complete
            slip_state.snap_progress = 1.0f;
            slip_state.snap_pending = false;
            result_position = slip_state.snap_target_bytes;
        } else {
            // Interpolate between current and target position
            // Use smooth ease-out curve for natural feel
            float t = slip_state.snap_progress;
            float smooth_t = 1.0f - (1.0f - t) * (1.0f - t);  // Ease-out quadratic
            
            int64_t delta = (int64_t)slip_state.snap_target_bytes - (int64_t)current_position_bytes;
            result_position = current_position_bytes + (uint64_t)(delta * smooth_t);
        }
    }
    
    if (slip_mutex) xSemaphoreGive(slip_mutex);
    
    return result_position;
}

// ============================================================================
// Integration Helpers Implementation
// ============================================================================

bool slip_mode_should_snap(void) {
    // Snap should occur if:
    // 1. Slip mode is enabled
    // 2. A slip was active (and just ended) OR snap is pending
    return slip_state.enabled && (slip_state.snap_pending || slip_state.active);
}

uint64_t slip_mode_get_snap_target(uint64_t current_position_bytes) {
    if (!slip_state.enabled) {
        return current_position_bytes;
    }
    
    if (slip_state.active) {
        // Slip is still active - return calculated background position
        return calculate_background_position();
    } else if (slip_state.snap_pending) {
        // Snap is pending - return the target
        return slip_state.snap_target_bytes;
    }
    
    return current_position_bytes;
}

const slip_mode_state_t* slip_mode_get_state(void) {
    return &slip_state;
}
