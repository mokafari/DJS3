/**
 * @file loop_control.c
 * @brief Loop control with auto-loop, loop roll, and beat-quantized operations
 */

#include "loop_control.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "loop_control";

// ============================================================================
// State
// ============================================================================

static struct {
    // Loop points (milliseconds for precision)
    uint32_t loop_in_ms;
    uint32_t loop_out_ms;
    
    // State
    loop_state_t state;
    loop_size_t current_size;
    
    // BPM for beat calculations
    float bpm;
    
    // Loop roll state
    struct {
        bool active;
        uint32_t start_position_ms;   // Where roll started
        uint32_t background_start_ms; // Background timeline start
        uint32_t saved_loop_in_ms;    // Original loop in (if any)
        uint32_t saved_loop_out_ms;   // Original loop out (if any)
        loop_state_t saved_state;     // State before roll
    } roll;
} s_loop = {
    .loop_in_ms = 0,
    .loop_out_ms = 0,
    .state = LOOP_STATE_INACTIVE,
    .current_size = LOOP_SIZE_4,
    .bpm = 120.0f,  // Default BPM
    .roll = {0}
};

// ============================================================================
// Loop Size Tables
// ============================================================================

/**
 * @brief Beat multipliers for each loop size
 */
static const float LOOP_SIZE_BEATS[] = {
    [LOOP_SIZE_1_32] = 0.03125f,   // 1/32
    [LOOP_SIZE_1_16] = 0.0625f,    // 1/16
    [LOOP_SIZE_1_8]  = 0.125f,     // 1/8
    [LOOP_SIZE_1_4]  = 0.25f,      // 1/4
    [LOOP_SIZE_1_2]  = 0.5f,       // 1/2
    [LOOP_SIZE_1]    = 1.0f,       // 1
    [LOOP_SIZE_2]    = 2.0f,       // 2
    [LOOP_SIZE_4]    = 4.0f,       // 4
    [LOOP_SIZE_8]    = 8.0f,       // 8
    [LOOP_SIZE_16]   = 16.0f,      // 16
    [LOOP_SIZE_32]   = 32.0f       // 32
};

/**
 * @brief Display names for loop sizes
 */
static const char* LOOP_SIZE_NAMES[] = {
    [LOOP_SIZE_1_32] = "1/32",
    [LOOP_SIZE_1_16] = "1/16",
    [LOOP_SIZE_1_8]  = "1/8",
    [LOOP_SIZE_1_4]  = "1/4",
    [LOOP_SIZE_1_2]  = "1/2",
    [LOOP_SIZE_1]    = "1",
    [LOOP_SIZE_2]    = "2",
    [LOOP_SIZE_4]    = "4",
    [LOOP_SIZE_8]    = "8",
    [LOOP_SIZE_16]   = "16",
    [LOOP_SIZE_32]   = "32"
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Calculate milliseconds for a given number of beats
 */
static uint32_t beats_to_ms(float beats) {
    if (s_loop.bpm < 1.0f) {
        return 0;
    }
    // ms = (beats / bpm) * 60 * 1000
    return (uint32_t)((beats / s_loop.bpm) * 60000.0f);
}

/**
 * @brief Quantize position to nearest beat
 */
static uint32_t quantize_to_beat(uint32_t position_ms) {
    uint32_t ms_per_beat = beats_to_ms(1.0f);
    if (ms_per_beat == 0) {
        return position_ms;
    }
    // Round to nearest beat
    uint32_t beat_num = (position_ms + (ms_per_beat / 2)) / ms_per_beat;
    return beat_num * ms_per_beat;
}

// ============================================================================
// Basic Loop Control (Legacy API)
// ============================================================================

bool loop_control_set_in(uint32_t position) {
    return loop_control_set_in_ms(position * 1000);
}

bool loop_control_set_out(uint32_t position) {
    return loop_control_set_out_ms(position * 1000);
}

uint32_t loop_control_get_in(void) {
    return s_loop.loop_in_ms / 1000;
}

uint32_t loop_control_get_out(void) {
    return s_loop.loop_out_ms / 1000;
}

uint32_t loop_control_get_length(void) {
    return loop_control_get_length_ms() / 1000;
}

bool loop_control_is_active(void) {
    return s_loop.state == LOOP_STATE_ACTIVE || s_loop.state == LOOP_STATE_ROLL;
}

void loop_control_clear(void) {
    s_loop.loop_in_ms = 0;
    s_loop.loop_out_ms = 0;
    s_loop.state = LOOP_STATE_INACTIVE;
    
    // Also clear any active roll
    if (s_loop.roll.active) {
        s_loop.roll.active = false;
    }
    
    ESP_LOGI(TAG, "Loop cleared");
}

// ============================================================================
// High-Precision API (milliseconds)
// ============================================================================

bool loop_control_set_in_ms(uint32_t position_ms) {
    s_loop.loop_in_ms = position_ms;
    
    // Validate: out must be after in
    if (s_loop.loop_out_ms > 0 && s_loop.loop_out_ms <= s_loop.loop_in_ms) {
        ESP_LOGW(TAG, "Loop out must be after loop in");
        return false;
    }
    
    // Activate if both points set
    if (s_loop.loop_in_ms > 0 && s_loop.loop_out_ms > 0) {
        s_loop.state = LOOP_STATE_ACTIVE;
    }
    
    ESP_LOGI(TAG, "Loop in set at %lu ms", (unsigned long)position_ms);
    return true;
}

bool loop_control_set_out_ms(uint32_t position_ms) {
    s_loop.loop_out_ms = position_ms;
    
    // Validate: out must be after in
    if (s_loop.loop_in_ms > 0 && s_loop.loop_out_ms <= s_loop.loop_in_ms) {
        ESP_LOGW(TAG, "Loop out must be after loop in");
        return false;
    }
    
    // Activate if both points set
    if (s_loop.loop_in_ms > 0 && s_loop.loop_out_ms > 0) {
        s_loop.state = LOOP_STATE_ACTIVE;
    }
    
    ESP_LOGI(TAG, "Loop out set at %lu ms", (unsigned long)position_ms);
    return true;
}

uint32_t loop_control_get_in_ms(void) {
    return s_loop.loop_in_ms;
}

uint32_t loop_control_get_out_ms(void) {
    return s_loop.loop_out_ms;
}

uint32_t loop_control_get_length_ms(void) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return 0;
    }
    if (s_loop.loop_out_ms <= s_loop.loop_in_ms) {
        return 0;
    }
    return s_loop.loop_out_ms - s_loop.loop_in_ms;
}

// ============================================================================
// BPM Integration
// ============================================================================

void loop_control_set_bpm(float bpm) {
    if (bpm >= 20.0f && bpm <= 300.0f) {
        s_loop.bpm = bpm;
        ESP_LOGD(TAG, "BPM set to %.1f", bpm);
    }
}

float loop_control_get_bpm(void) {
    return s_loop.bpm;
}

uint32_t loop_control_get_ms_per_beat(void) {
    return beats_to_ms(1.0f);
}

// ============================================================================
// Auto-Loop
// ============================================================================

bool loop_control_auto_loop(loop_size_t size, uint32_t current_position_ms) {
    if (size >= LOOP_SIZE_COUNT) {
        return false;
    }
    
    float beats = LOOP_SIZE_BEATS[size];
    uint32_t loop_length_ms = beats_to_ms(beats);
    
    if (loop_length_ms == 0) {
        ESP_LOGW(TAG, "Cannot create loop: invalid BPM");
        return false;
    }
    
    // Quantize start position to nearest beat
    uint32_t loop_start = quantize_to_beat(current_position_ms);
    uint32_t loop_end = loop_start + loop_length_ms;
    
    s_loop.loop_in_ms = loop_start;
    s_loop.loop_out_ms = loop_end;
    s_loop.current_size = size;
    s_loop.state = LOOP_STATE_ACTIVE;
    
    ESP_LOGI(TAG, "Auto-loop: %s beats (%lu-%lu ms)", 
             LOOP_SIZE_NAMES[size],
             (unsigned long)loop_start,
             (unsigned long)loop_end);
    
    return true;
}

loop_size_t loop_control_get_size(void) {
    return s_loop.current_size;
}

void loop_control_set_size(loop_size_t size) {
    if (size < LOOP_SIZE_COUNT) {
        s_loop.current_size = size;
    }
}

bool loop_control_double(void) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return false;
    }
    
    // Find current size and go to next
    if (s_loop.current_size >= LOOP_SIZE_32) {
        ESP_LOGW(TAG, "Loop already at maximum size");
        return false;
    }
    
    uint32_t current_length = s_loop.loop_out_ms - s_loop.loop_in_ms;
    s_loop.loop_out_ms = s_loop.loop_in_ms + (current_length * 2);
    
    // Update size tracker
    if (s_loop.current_size < LOOP_SIZE_32) {
        s_loop.current_size++;
    }
    
    ESP_LOGI(TAG, "Loop doubled: now %s beats", LOOP_SIZE_NAMES[s_loop.current_size]);
    return true;
}

bool loop_control_halve(void) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return false;
    }
    
    if (s_loop.current_size <= LOOP_SIZE_1_32) {
        ESP_LOGW(TAG, "Loop already at minimum size");
        return false;
    }
    
    uint32_t current_length = s_loop.loop_out_ms - s_loop.loop_in_ms;
    uint32_t new_length = current_length / 2;
    
    // Minimum loop length: 10ms
    if (new_length < 10) {
        return false;
    }
    
    s_loop.loop_out_ms = s_loop.loop_in_ms + new_length;
    
    // Update size tracker
    if (s_loop.current_size > LOOP_SIZE_1_32) {
        s_loop.current_size--;
    }
    
    ESP_LOGI(TAG, "Loop halved: now %s beats", LOOP_SIZE_NAMES[s_loop.current_size]);
    return true;
}

float loop_control_size_to_beats(loop_size_t size) {
    if (size >= LOOP_SIZE_COUNT) {
        return 1.0f;
    }
    return LOOP_SIZE_BEATS[size];
}

const char* loop_control_size_name(loop_size_t size) {
    if (size >= LOOP_SIZE_COUNT) {
        return "?";
    }
    return LOOP_SIZE_NAMES[size];
}

// ============================================================================
// Loop Roll
// ============================================================================

bool loop_control_roll_start(loop_size_t size, uint32_t current_position_ms) {
    if (size >= LOOP_SIZE_COUNT) {
        return false;
    }
    
    // Save current loop state
    s_loop.roll.saved_loop_in_ms = s_loop.loop_in_ms;
    s_loop.roll.saved_loop_out_ms = s_loop.loop_out_ms;
    s_loop.roll.saved_state = s_loop.state;
    
    // Store where we started and the background timeline
    s_loop.roll.start_position_ms = current_position_ms;
    s_loop.roll.background_start_ms = current_position_ms;
    s_loop.roll.active = true;
    
    // Create the roll loop
    float beats = LOOP_SIZE_BEATS[size];
    uint32_t roll_length_ms = beats_to_ms(beats);
    
    if (roll_length_ms < 10) {
        roll_length_ms = 10;  // Minimum 10ms
    }
    
    // Quantize to beat
    uint32_t roll_start = quantize_to_beat(current_position_ms);
    
    s_loop.loop_in_ms = roll_start;
    s_loop.loop_out_ms = roll_start + roll_length_ms;
    s_loop.current_size = size;
    s_loop.state = LOOP_STATE_ROLL;
    
    ESP_LOGI(TAG, "Loop roll started: %s beats at %lu ms", 
             LOOP_SIZE_NAMES[size],
             (unsigned long)roll_start);
    
    return true;
}

uint32_t loop_control_roll_release(uint32_t elapsed_ms) {
    if (!s_loop.roll.active) {
        return 0;
    }
    
    // Calculate where background playback would be
    uint32_t background_position = s_loop.roll.background_start_ms + elapsed_ms;
    
    // Restore previous loop state
    s_loop.loop_in_ms = s_loop.roll.saved_loop_in_ms;
    s_loop.loop_out_ms = s_loop.roll.saved_loop_out_ms;
    s_loop.state = s_loop.roll.saved_state;
    
    // Clear roll state
    s_loop.roll.active = false;
    memset(&s_loop.roll, 0, sizeof(s_loop.roll));
    
    ESP_LOGI(TAG, "Loop roll released: returning to %lu ms", 
             (unsigned long)background_position);
    
    return background_position;
}

bool loop_control_is_roll_active(void) {
    return s_loop.roll.active;
}

uint32_t loop_control_get_background_position(uint32_t elapsed_ms) {
    if (!s_loop.roll.active) {
        return 0;
    }
    return s_loop.roll.background_start_ms + elapsed_ms;
}

// ============================================================================
// Loop Move/Shift
// ============================================================================

bool loop_control_move_forward(float beats) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return false;
    }
    
    uint32_t shift_ms = beats_to_ms(beats);
    if (shift_ms == 0) {
        return false;
    }
    
    s_loop.loop_in_ms += shift_ms;
    s_loop.loop_out_ms += shift_ms;
    
    ESP_LOGI(TAG, "Loop moved forward %.2f beats to %lu-%lu ms",
             beats,
             (unsigned long)s_loop.loop_in_ms,
             (unsigned long)s_loop.loop_out_ms);
    
    return true;
}

bool loop_control_move_backward(float beats) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return false;
    }
    
    uint32_t shift_ms = beats_to_ms(beats);
    if (shift_ms == 0) {
        return false;
    }
    
    // Don't move before start of track
    if (shift_ms > s_loop.loop_in_ms) {
        shift_ms = s_loop.loop_in_ms;
    }
    
    s_loop.loop_in_ms -= shift_ms;
    s_loop.loop_out_ms -= shift_ms;
    
    ESP_LOGI(TAG, "Loop moved backward %.2f beats to %lu-%lu ms",
             beats,
             (unsigned long)s_loop.loop_in_ms,
             (unsigned long)s_loop.loop_out_ms);
    
    return true;
}

bool loop_control_move_next(void) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return false;
    }
    
    uint32_t loop_length = s_loop.loop_out_ms - s_loop.loop_in_ms;
    s_loop.loop_in_ms += loop_length;
    s_loop.loop_out_ms += loop_length;
    
    ESP_LOGI(TAG, "Loop moved to next segment: %lu-%lu ms",
             (unsigned long)s_loop.loop_in_ms,
             (unsigned long)s_loop.loop_out_ms);
    
    return true;
}

bool loop_control_move_prev(void) {
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return false;
    }
    
    uint32_t loop_length = s_loop.loop_out_ms - s_loop.loop_in_ms;
    
    // Don't move before start
    if (loop_length > s_loop.loop_in_ms) {
        return false;
    }
    
    s_loop.loop_in_ms -= loop_length;
    s_loop.loop_out_ms -= loop_length;
    
    ESP_LOGI(TAG, "Loop moved to prev segment: %lu-%lu ms",
             (unsigned long)s_loop.loop_in_ms,
             (unsigned long)s_loop.loop_out_ms);
    
    return true;
}

// ============================================================================
// Loop State
// ============================================================================

loop_state_t loop_control_get_state(void) {
    return s_loop.state;
}

bool loop_control_toggle(void) {
    if (s_loop.loop_in_ms == 0 || s_loop.loop_out_ms == 0) {
        return false;
    }
    
    if (s_loop.state == LOOP_STATE_ACTIVE) {
        s_loop.state = LOOP_STATE_INACTIVE;
        ESP_LOGI(TAG, "Loop deactivated");
        return false;
    } else if (s_loop.state == LOOP_STATE_INACTIVE) {
        s_loop.state = LOOP_STATE_ACTIVE;
        ESP_LOGI(TAG, "Loop activated");
        return true;
    }
    
    // Don't toggle during roll
    return loop_control_is_active();
}

void loop_control_activate(void) {
    if (s_loop.loop_in_ms > 0 && s_loop.loop_out_ms > 0) {
        s_loop.state = LOOP_STATE_ACTIVE;
    }
}

void loop_control_deactivate(void) {
    if (s_loop.state == LOOP_STATE_ACTIVE) {
        s_loop.state = LOOP_STATE_INACTIVE;
    }
}

// ============================================================================
// Playback Integration
// ============================================================================

uint32_t loop_control_check_loop_point(uint32_t current_position_ms) {
    // No active loop
    if (s_loop.state == LOOP_STATE_INACTIVE) {
        return 0;
    }
    
    // Check if we've passed the loop out point
    if (current_position_ms >= s_loop.loop_out_ms) {
        ESP_LOGD(TAG, "Loop point reached, jumping to %lu ms", 
                 (unsigned long)s_loop.loop_in_ms);
        return s_loop.loop_in_ms;
    }
    
    return 0;
}
