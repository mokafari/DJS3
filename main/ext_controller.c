/**
 * @file ext_controller.c
 * @brief External controller support implementation
 * 
 * Implements USB HID host, MIDI input, button matrix scanning, and
 * rotary encoder handling with unified event queue and mapping system.
 */

#include "ext_controller.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <math.h>

#ifndef USB_HOST_DISABLE
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#endif

static const char *TAG = "ext_ctrl";

/* ============================================================================
 * NVS Keys
 * ============================================================================ */

#define NVS_NAMESPACE           "ext_ctrl"
#define NVS_KEY_ACTIVE_PROFILE  "active_prof"
#define NVS_KEY_PROFILE_PREFIX  "profile_"

/* ============================================================================
 * Internal State
 * ============================================================================ */

/**
 * @brief Encoder internal state
 */
typedef struct {
    bool configured;
    ext_ctrl_encoder_config_t config;
    
    // State
    int8_t last_a;
    int8_t last_b;
    int16_t delta_accum;
    bool button_state;
    bool button_last;
    uint32_t button_change_time;
    
    // Acceleration
    uint32_t last_step_time;
    uint8_t current_accel;
} encoder_state_t;

/**
 * @brief Button matrix state
 */
typedef struct {
    bool initialized;
    ext_ctrl_matrix_config_t config;
    
    // State arrays
    bool current_state[EXT_CTRL_MATRIX_MAX_ROWS][EXT_CTRL_MATRIX_MAX_COLS];
    bool last_state[EXT_CTRL_MATRIX_MAX_ROWS][EXT_CTRL_MATRIX_MAX_COLS];
    uint32_t change_time[EXT_CTRL_MATRIX_MAX_ROWS][EXT_CTRL_MATRIX_MAX_COLS];
} matrix_state_t;

/**
 * @brief MIDI parser state
 */
typedef struct {
    bool enabled;
    int uart_num;
    
    // Running status
    uint8_t running_status;
    uint8_t data_bytes[2];
    uint8_t data_count;
    uint8_t expected_count;
    
    // SysEx buffer
    uint8_t sysex_buffer[128];
    uint8_t sysex_len;
    bool in_sysex;
} midi_state_t;

/**
 * @brief Learn mode state
 */
typedef struct {
    bool active;
    ext_ctrl_action_t target_action;
    uint8_t target_deck;
    ext_ctrl_learn_cb_t callback;
    void *callback_arg;
    
    bool input_captured;
    ext_ctrl_input_id_t captured_input;
} learn_state_t;

/**
 * @brief Module state
 */
static struct {
    bool initialized;
    
    // Event queues
    QueueHandle_t raw_event_queue;
    QueueHandle_t action_event_queue;
    SemaphoreHandle_t mutex;
    
    // Callbacks
    ext_ctrl_raw_cb_t raw_callback;
    void *raw_callback_arg;
    ext_ctrl_action_cb_t action_callback;
    void *action_callback_arg;
    
    // Controller states
    encoder_state_t encoders[EXT_CTRL_MAX_ENCODERS];
    matrix_state_t matrix;
    midi_state_t midi;
    learn_state_t learn;
    
    // Active profile
    ext_ctrl_profile_t active_profile;
    bool profile_loaded;
    
    // Shift state
    bool shift_held;
    
    // Statistics
    uint32_t events_received;
    uint32_t events_mapped;
    uint32_t events_dropped;
    
    // USB HID
    bool hid_running;
    uint8_t hid_device_count;
    ext_ctrl_hid_device_t hid_devices[4];
    
    // NVS handle
    nvs_handle_t nvs_handle;
    bool nvs_open;
} s_state = {0};

/* ============================================================================
 * Action Name Table
 * ============================================================================ */

static const char* ACTION_NAMES[] = {
    [EXT_ACTION_NONE] = "None",
    [EXT_ACTION_PLAY] = "Play",
    [EXT_ACTION_PAUSE] = "Pause",
    [EXT_ACTION_PLAY_PAUSE] = "Play/Pause",
    [EXT_ACTION_CUE] = "Cue",
    [EXT_ACTION_CUE_PLAY] = "Cue Play",
    [EXT_ACTION_SYNC] = "Sync",
    [EXT_ACTION_SYNC_TOGGLE] = "Sync Toggle",
    [EXT_ACTION_HOT_CUE_1] = "Hot Cue 1",
    [EXT_ACTION_HOT_CUE_2] = "Hot Cue 2",
    [EXT_ACTION_HOT_CUE_3] = "Hot Cue 3",
    [EXT_ACTION_HOT_CUE_4] = "Hot Cue 4",
    [EXT_ACTION_HOT_CUE_5] = "Hot Cue 5",
    [EXT_ACTION_HOT_CUE_6] = "Hot Cue 6",
    [EXT_ACTION_HOT_CUE_7] = "Hot Cue 7",
    [EXT_ACTION_HOT_CUE_8] = "Hot Cue 8",
    [EXT_ACTION_LOOP_IN] = "Loop In",
    [EXT_ACTION_LOOP_OUT] = "Loop Out",
    [EXT_ACTION_LOOP_TOGGLE] = "Loop Toggle",
    [EXT_ACTION_LOOP_DOUBLE] = "Loop Double",
    [EXT_ACTION_LOOP_HALVE] = "Loop Halve",
    [EXT_ACTION_LOOP_ROLL_1_16] = "Loop Roll 1/16",
    [EXT_ACTION_LOOP_ROLL_1_8] = "Loop Roll 1/8",
    [EXT_ACTION_LOOP_ROLL_1_4] = "Loop Roll 1/4",
    [EXT_ACTION_LOOP_ROLL_1_2] = "Loop Roll 1/2",
    [EXT_ACTION_LOOP_ROLL_1] = "Loop Roll 1",
    [EXT_ACTION_LOOP_ROLL_2] = "Loop Roll 2",
    [EXT_ACTION_LOOP_ROLL_4] = "Loop Roll 4",
    [EXT_ACTION_JOG_TOUCH] = "Jog Touch",
    [EXT_ACTION_JOG_TURN] = "Jog Turn",
    [EXT_ACTION_JOG_SCRATCH] = "Jog Scratch",
    [EXT_ACTION_SLIP_MODE] = "Slip Mode",
    [EXT_ACTION_PITCH_FADER] = "Pitch Fader",
    [EXT_ACTION_PITCH_BEND_PLUS] = "Pitch Bend +",
    [EXT_ACTION_PITCH_BEND_MINUS] = "Pitch Bend -",
    [EXT_ACTION_TEMPO_UP] = "Tempo Up",
    [EXT_ACTION_TEMPO_DOWN] = "Tempo Down",
    [EXT_ACTION_KEY_LOCK] = "Key Lock",
    [EXT_ACTION_EQ_LOW] = "EQ Low",
    [EXT_ACTION_EQ_MID] = "EQ Mid",
    [EXT_ACTION_EQ_HIGH] = "EQ High",
    [EXT_ACTION_FILTER] = "Filter",
    [EXT_ACTION_FX_1] = "FX 1",
    [EXT_ACTION_FX_2] = "FX 2",
    [EXT_ACTION_FX_3] = "FX 3",
    [EXT_ACTION_FX_WET_DRY] = "FX Wet/Dry",
    [EXT_ACTION_FX_PARAM_1] = "FX Param 1",
    [EXT_ACTION_FX_PARAM_2] = "FX Param 2",
    [EXT_ACTION_TRACK_NEXT] = "Track Next",
    [EXT_ACTION_TRACK_PREV] = "Track Prev",
    [EXT_ACTION_LOAD_TRACK] = "Load Track",
    [EXT_ACTION_EJECT] = "Eject",
    [EXT_ACTION_BROWSE_UP] = "Browse Up",
    [EXT_ACTION_BROWSE_DOWN] = "Browse Down",
    [EXT_ACTION_BROWSE_SELECT] = "Browse Select",
    [EXT_ACTION_BROWSE_BACK] = "Browse Back",
    [EXT_ACTION_CROSSFADER] = "Crossfader",
    [EXT_ACTION_CHANNEL_FADER] = "Channel Fader",
    [EXT_ACTION_MASTER_VOLUME] = "Master Volume",
    [EXT_ACTION_CUE_MIX] = "Cue Mix",
    [EXT_ACTION_HEADPHONE_VOLUME] = "Headphone Vol",
    [EXT_ACTION_SHIFT] = "Shift",
    [EXT_ACTION_DECK_SELECT] = "Deck Select",
    [EXT_ACTION_VINYL_MODE] = "Vinyl Mode",
};

static const char* SOURCE_NAMES[] = {
    [EXT_CTRL_SOURCE_NONE] = "None",
    [EXT_CTRL_SOURCE_USB_HID] = "USB HID",
    [EXT_CTRL_SOURCE_MIDI] = "MIDI",
    [EXT_CTRL_SOURCE_MATRIX] = "Matrix",
    [EXT_CTRL_SOURCE_ENCODER] = "Encoder",
    [EXT_CTRL_SOURCE_ANALOG] = "Analog",
};

static const char* ELEMENT_NAMES[] = {
    [EXT_CTRL_ELEM_BUTTON] = "Button",
    [EXT_CTRL_ELEM_TOGGLE] = "Toggle",
    [EXT_CTRL_ELEM_ENCODER] = "Encoder",
    [EXT_CTRL_ELEM_FADER] = "Fader",
    [EXT_CTRL_ELEM_KNOB] = "Knob",
    [EXT_CTRL_ELEM_JOGWHEEL] = "Jogwheel",
    [EXT_CTRL_ELEM_PAD] = "Pad",
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief Queue a raw event
 */
static bool queue_raw_event(const ext_ctrl_event_t *event) {
    if (!s_state.raw_event_queue) {
        return false;
    }
    
    s_state.events_received++;
    
    if (xQueueSend(s_state.raw_event_queue, event, 0) != pdTRUE) {
        s_state.events_dropped++;
        return false;
    }
    
    return true;
}

/**
 * @brief Queue an action event
 */
static bool queue_action_event(const ext_ctrl_action_event_t *event) {
    if (!s_state.action_event_queue) {
        return false;
    }
    
    s_state.events_mapped++;
    
    if (xQueueSend(s_state.action_event_queue, event, 0) != pdTRUE) {
        s_state.events_dropped++;
        return false;
    }
    
    return true;
}

/**
 * @brief Check if input matches a mapping's input criteria
 */
static bool input_matches_mapping(const ext_ctrl_event_t *event, 
                                   const ext_ctrl_mapping_t *mapping) {
    if (event->source != mapping->input.source) {
        return false;
    }
    
    if (event->element != mapping->input.element) {
        return false;
    }
    
    switch (event->source) {
        case EXT_CTRL_SOURCE_MIDI:
            // Check MIDI channel (0xFF = any)
            if (mapping->input.midi_channel != 0xFF &&
                event->data.midi.channel != mapping->input.midi_channel) {
                return false;
            }
            // Check MIDI type
            if ((event->data.midi.type & 0xF0) != mapping->input.midi_type) {
                return false;
            }
            // Check note/CC number
            if (event->data.midi.data1 != mapping->input.midi_note_cc) {
                return false;
            }
            return true;
            
        case EXT_CTRL_SOURCE_MATRIX:
        case EXT_CTRL_SOURCE_ENCODER:
            return event->element_id == mapping->input.element_id;
            
        case EXT_CTRL_SOURCE_USB_HID:
            if (event->device_id != mapping->input.device_id) {
                return false;
            }
            return event->element_id == mapping->input.element_id;
            
        default:
            return event->element_id == mapping->input.element_id;
    }
}

/**
 * @brief Apply value transformation from mapping
 */
static float transform_value(uint16_t raw, const ext_ctrl_mapping_t *mapping) {
    float value = (float)raw;
    
    // Apply min/max range
    if (mapping->max_value > mapping->min_value) {
        value = (value - mapping->min_value) / 
                (float)(mapping->max_value - mapping->min_value);
        value = fmaxf(0.0f, fminf(1.0f, value));
    }
    
    // Apply inversion
    if (mapping->invert) {
        value = 1.0f - value;
    }
    
    // Apply sensitivity (for relative values)
    if (mapping->sensitivity > 1) {
        value *= (float)mapping->sensitivity;
    }
    
    return value;
}

/**
 * @brief Process raw event through profile mapping
 */
static void process_event_mapping(const ext_ctrl_event_t *event) {
    if (!s_state.profile_loaded) {
        return;
    }
    
    // Handle learn mode
    if (s_state.learn.active) {
        // Capture the input
        s_state.learn.captured_input.source = event->source;
        s_state.learn.captured_input.device_id = event->device_id;
        s_state.learn.captured_input.element = event->element;
        s_state.learn.captured_input.element_id = event->element_id;
        
        if (event->source == EXT_CTRL_SOURCE_MIDI) {
            s_state.learn.captured_input.midi_channel = event->data.midi.channel;
            s_state.learn.captured_input.midi_type = event->data.midi.type & 0xF0;
            s_state.learn.captured_input.midi_note_cc = event->data.midi.data1;
        }
        
        s_state.learn.input_captured = true;
        
        // Invoke callback
        if (s_state.learn.callback) {
            s_state.learn.callback(&s_state.learn.captured_input, 
                                   s_state.learn.callback_arg);
        }
        return;
    }
    
    // Search for matching mapping
    const ext_ctrl_profile_t *profile = &s_state.active_profile;
    
    for (int i = 0; i < profile->mapping_count; i++) {
        const ext_ctrl_mapping_t *mapping = &profile->mappings[i];
        
        if (!mapping->enabled) {
            continue;
        }
        
        if (!input_matches_mapping(event, mapping)) {
            continue;
        }
        
        // Found matching mapping - create action event
        ext_ctrl_action_event_t action_event = {0};
        action_event.timestamp = event->timestamp;
        action_event.shift_held = s_state.shift_held;
        action_event.deck = mapping->target_deck;
        
        // Select action based on shift state
        action_event.action = s_state.shift_held && mapping->shift_action != EXT_ACTION_NONE
                              ? mapping->shift_action 
                              : mapping->action;
        
        // Set value based on element type
        switch (event->element) {
            case EXT_CTRL_ELEM_BUTTON:
            case EXT_CTRL_ELEM_TOGGLE:
            case EXT_CTRL_ELEM_PAD:
                action_event.value.pressed = event->data.button.pressed;
                break;
                
            case EXT_CTRL_ELEM_ENCODER:
            case EXT_CTRL_ELEM_JOGWHEEL:
                action_event.value.relative = event->data.encoder.delta;
                if (mapping->sensitivity > 1) {
                    action_event.value.relative *= mapping->sensitivity;
                }
                break;
                
            case EXT_CTRL_ELEM_FADER:
            case EXT_CTRL_ELEM_KNOB:
                action_event.value.normalized = transform_value(event->data.analog.value, mapping);
                action_event.value.absolute = event->data.analog.value;
                break;
                
            default:
                break;
        }
        
        // Handle shift key specially
        if (action_event.action == EXT_ACTION_SHIFT) {
            s_state.shift_held = action_event.value.pressed;
        }
        
        // Queue the action
        queue_action_event(&action_event);
        
        // Don't break - multiple mappings might match
    }
}

/* ============================================================================
 * Rotary Encoder Implementation
 * ============================================================================ */

bool ext_ctrl_encoder_init(uint8_t encoder_id, const ext_ctrl_encoder_config_t *config) {
    if (encoder_id >= EXT_CTRL_MAX_ENCODERS || !config) {
        return false;
    }
    
    encoder_state_t *enc = &s_state.encoders[encoder_id];
    
    // Configure GPIO for encoder A
    if (config->pin_a >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << config->pin_a),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }
    
    // Configure GPIO for encoder B
    if (config->pin_b >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << config->pin_b),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }
    
    // Configure push button if present
    if (config->pin_button >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << config->pin_button),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }
    
    // Store configuration
    memcpy(&enc->config, config, sizeof(ext_ctrl_encoder_config_t));
    enc->configured = true;
    
    // Initialize state
    if (config->pin_a >= 0) {
        enc->last_a = gpio_get_level(config->pin_a);
    }
    if (config->pin_b >= 0) {
        enc->last_b = gpio_get_level(config->pin_b);
    }
    enc->delta_accum = 0;
    enc->button_state = false;
    enc->button_last = false;
    enc->button_change_time = 0;
    enc->last_step_time = get_time_ms();
    enc->current_accel = 1;
    
    ESP_LOGI(TAG, "Encoder %d initialized: A=%d, B=%d, BTN=%d", 
             encoder_id, config->pin_a, config->pin_b, config->pin_button);
    
    return true;
}

void ext_ctrl_encoder_deinit(uint8_t encoder_id) {
    if (encoder_id >= EXT_CTRL_MAX_ENCODERS) {
        return;
    }
    
    s_state.encoders[encoder_id].configured = false;
}

void ext_ctrl_encoder_update(void) {
    uint32_t now = get_time_ms();
    
    for (int i = 0; i < EXT_CTRL_MAX_ENCODERS; i++) {
        encoder_state_t *enc = &s_state.encoders[i];
        
        if (!enc->configured) {
            continue;
        }
        
        const ext_ctrl_encoder_config_t *cfg = &enc->config;
        
        // Read encoder pins
        int8_t a = (cfg->pin_a >= 0) ? gpio_get_level(cfg->pin_a) : 0;
        int8_t b = (cfg->pin_b >= 0) ? gpio_get_level(cfg->pin_b) : 0;
        
        // Quadrature decoding
        if (a != enc->last_a || b != enc->last_b) {
            int8_t state = (enc->last_a << 1) | enc->last_b;
            int8_t new_state = (a << 1) | b;
            
            int8_t delta = 0;
            
            // State transitions
            if (state == 0 && new_state == 1) delta = 1;
            else if (state == 1 && new_state == 3) delta = 1;
            else if (state == 3 && new_state == 2) delta = 1;
            else if (state == 2 && new_state == 0) delta = 1;
            else if (state == 0 && new_state == 2) delta = -1;
            else if (state == 2 && new_state == 3) delta = -1;
            else if (state == 3 && new_state == 1) delta = -1;
            else if (state == 1 && new_state == 0) delta = -1;
            
            if (delta != 0) {
                // Apply direction reversal if configured
                if (cfg->reverse_direction) {
                    delta = -delta;
                }
                
                // Calculate acceleration
                uint8_t accel = 1;
                if (cfg->acceleration_enabled) {
                    uint32_t time_since_last = now - enc->last_step_time;
                    
                    if (time_since_last < cfg->accel_threshold_ms) {
                        // Fast movement - increase acceleration
                        if (enc->current_accel < cfg->accel_max_multiplier) {
                            enc->current_accel++;
                        }
                    } else {
                        // Slow movement - reset acceleration
                        enc->current_accel = 1;
                    }
                    
                    accel = enc->current_accel;
                }
                
                // Apply pulses per detent
                if (cfg->pulses_per_detent > 1) {
                    // Accumulate until we hit a detent
                    enc->delta_accum += delta;
                    if (abs(enc->delta_accum) >= cfg->pulses_per_detent) {
                        delta = (enc->delta_accum > 0) ? 1 : -1;
                        delta *= accel;
                        enc->delta_accum = 0;
                    } else {
                        delta = 0;
                    }
                } else {
                    delta *= accel;
                }
                
                // Generate event if we have movement
                if (delta != 0) {
                    ext_ctrl_event_t event = {
                        .source = EXT_CTRL_SOURCE_ENCODER,
                        .element = EXT_CTRL_ELEM_ENCODER,
                        .device_id = 0,
                        .element_id = i,
                        .data.encoder = {
                            .delta = delta,
                            .acceleration = accel
                        },
                        .timestamp = now
                    };
                    
                    queue_raw_event(&event);
                }
                
                enc->last_step_time = now;
            }
            
            enc->last_a = a;
            enc->last_b = b;
        }
        
        // Handle push button with debouncing
        if (cfg->pin_button >= 0) {
            bool current = !gpio_get_level(cfg->pin_button); // Active low
            
            if (current != enc->button_last) {
                enc->button_change_time = now;
            }
            
            if ((now - enc->button_change_time) > EXT_CTRL_DEBOUNCE_MS) {
                if (current != enc->button_state) {
                    enc->button_state = current;
                    
                    // Generate button event
                    ext_ctrl_event_t event = {
                        .source = EXT_CTRL_SOURCE_ENCODER,
                        .element = EXT_CTRL_ELEM_BUTTON,
                        .device_id = 0,
                        .element_id = i,
                        .data.button = {
                            .pressed = current,
                            .velocity = current ? 127 : 0
                        },
                        .timestamp = now
                    };
                    
                    queue_raw_event(&event);
                }
            }
            
            enc->button_last = current;
        }
    }
}

int16_t ext_ctrl_encoder_get_delta(uint8_t encoder_id) {
    if (encoder_id >= EXT_CTRL_MAX_ENCODERS) {
        return 0;
    }
    
    // Note: For direct polling, not queue-based
    // This accumulator is separate from the event queue
    return 0; // Would need separate accumulator
}

bool ext_ctrl_encoder_get_button(uint8_t encoder_id) {
    if (encoder_id >= EXT_CTRL_MAX_ENCODERS) {
        return false;
    }
    
    return s_state.encoders[encoder_id].button_state;
}

/* ============================================================================
 * Button Matrix Implementation
 * ============================================================================ */

bool ext_ctrl_matrix_init(const ext_ctrl_matrix_config_t *config) {
    if (!config) {
        return false;
    }
    
    if (config->num_rows > EXT_CTRL_MATRIX_MAX_ROWS ||
        config->num_cols > EXT_CTRL_MATRIX_MAX_COLS) {
        ESP_LOGE(TAG, "Matrix size %dx%d exceeds maximum %dx%d",
                 config->num_rows, config->num_cols,
                 EXT_CTRL_MATRIX_MAX_ROWS, EXT_CTRL_MATRIX_MAX_COLS);
        return false;
    }
    
    matrix_state_t *mat = &s_state.matrix;
    
    // Configure row pins as outputs
    uint64_t row_mask = 0;
    for (int i = 0; i < config->num_rows; i++) {
        if (config->row_pins[i] >= 0) {
            row_mask |= (1ULL << config->row_pins[i]);
        }
    }
    
    if (row_mask) {
        gpio_config_t row_conf = {
            .pin_bit_mask = row_mask,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&row_conf);
        
        // Set all rows high initially
        for (int i = 0; i < config->num_rows; i++) {
            if (config->row_pins[i] >= 0) {
                gpio_set_level(config->row_pins[i], 1);
            }
        }
    }
    
    // Configure column pins as inputs with pull-ups
    uint64_t col_mask = 0;
    for (int i = 0; i < config->num_cols; i++) {
        if (config->col_pins[i] >= 0) {
            col_mask |= (1ULL << config->col_pins[i]);
        }
    }
    
    if (col_mask) {
        gpio_config_t col_conf = {
            .pin_bit_mask = col_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&col_conf);
    }
    
    // Store configuration
    memcpy(&mat->config, config, sizeof(ext_ctrl_matrix_config_t));
    mat->initialized = true;
    
    // Initialize state
    memset(mat->current_state, 0, sizeof(mat->current_state));
    memset(mat->last_state, 0, sizeof(mat->last_state));
    memset(mat->change_time, 0, sizeof(mat->change_time));
    
    ESP_LOGI(TAG, "Button matrix initialized: %dx%d (debounce: %u ms)",
             config->num_rows, config->num_cols, config->debounce_ms);
    
    return true;
}

void ext_ctrl_matrix_deinit(void) {
    s_state.matrix.initialized = false;
}

void ext_ctrl_matrix_scan(void) {
    matrix_state_t *mat = &s_state.matrix;
    
    if (!mat->initialized) {
        return;
    }
    
    const ext_ctrl_matrix_config_t *cfg = &mat->config;
    uint32_t now = get_time_ms();
    uint16_t debounce_ms = cfg->debounce_ms ? cfg->debounce_ms : EXT_CTRL_DEBOUNCE_MS;
    
    // Scan each row
    for (int row = 0; row < cfg->num_rows; row++) {
        if (cfg->row_pins[row] < 0) continue;
        
        // Drive this row low
        gpio_set_level(cfg->row_pins[row], 0);
        
        // Small delay for signal settling (in practice, might need esp_rom_delay_us)
        for (volatile int d = 0; d < 10; d++) {}
        
        // Read all columns
        for (int col = 0; col < cfg->num_cols; col++) {
            if (cfg->col_pins[col] < 0) continue;
            
            bool raw = gpio_get_level(cfg->col_pins[col]);
            bool pressed = cfg->active_low ? !raw : raw;
            
            // Debouncing
            if (pressed != mat->last_state[row][col]) {
                mat->change_time[row][col] = now;
            }
            
            if ((now - mat->change_time[row][col]) > debounce_ms) {
                if (pressed != mat->current_state[row][col]) {
                    mat->current_state[row][col] = pressed;
                    
                    // Generate event
                    uint8_t button_id = row * cfg->num_cols + col;
                    
                    ext_ctrl_event_t event = {
                        .source = EXT_CTRL_SOURCE_MATRIX,
                        .element = EXT_CTRL_ELEM_BUTTON,
                        .device_id = 0,
                        .element_id = button_id,
                        .data.button = {
                            .pressed = pressed,
                            .velocity = pressed ? 127 : 0
                        },
                        .timestamp = now
                    };
                    
                    queue_raw_event(&event);
                    
                    ESP_LOGD(TAG, "Matrix button [%d,%d] = %s", 
                             row, col, pressed ? "PRESSED" : "released");
                }
            }
            
            mat->last_state[row][col] = pressed;
        }
        
        // Release row
        gpio_set_level(cfg->row_pins[row], 1);
    }
}

bool ext_ctrl_matrix_get_button(uint8_t row, uint8_t col) {
    if (!s_state.matrix.initialized) {
        return false;
    }
    
    if (row >= s_state.matrix.config.num_rows ||
        col >= s_state.matrix.config.num_cols) {
        return false;
    }
    
    return s_state.matrix.current_state[row][col];
}

/* ============================================================================
 * MIDI Implementation
 * ============================================================================ */

/**
 * @brief Get expected data bytes for MIDI status
 */
static uint8_t midi_data_bytes_for_status(uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: return 2;  // Note Off
        case 0x90: return 2;  // Note On
        case 0xA0: return 2;  // Aftertouch
        case 0xB0: return 2;  // Control Change
        case 0xC0: return 1;  // Program Change
        case 0xD0: return 1;  // Channel Pressure
        case 0xE0: return 2;  // Pitch Bend
        default:   return 0;
    }
}

bool ext_ctrl_midi_enable(int uart_num) {
    s_state.midi.enabled = true;
    s_state.midi.uart_num = uart_num;
    s_state.midi.running_status = 0;
    s_state.midi.data_count = 0;
    s_state.midi.expected_count = 0;
    s_state.midi.in_sysex = false;
    s_state.midi.sysex_len = 0;
    
    ESP_LOGI(TAG, "MIDI enabled (UART: %d)", uart_num);
    return true;
}

void ext_ctrl_midi_disable(void) {
    s_state.midi.enabled = false;
    ESP_LOGI(TAG, "MIDI disabled");
}

void ext_ctrl_midi_process(const uint8_t *data, size_t len) {
    if (!s_state.midi.enabled || !data || len == 0) {
        return;
    }
    
    midi_state_t *midi = &s_state.midi;
    uint32_t now = get_time_ms();
    
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        
        // Handle SysEx
        if (midi->in_sysex) {
            if (byte == 0xF7) {
                // End of SysEx
                midi->in_sysex = false;
                // Could generate SysEx event here if needed
            } else if (byte & 0x80) {
                // Unexpected status byte - abort SysEx
                midi->in_sysex = false;
                // Fall through to process the new status
            } else {
                // Data byte - accumulate
                if (midi->sysex_len < sizeof(midi->sysex_buffer)) {
                    midi->sysex_buffer[midi->sysex_len++] = byte;
                }
                continue;
            }
        }
        
        // Check for status byte
        if (byte & 0x80) {
            // Real-time messages (can occur anywhere)
            if (byte >= 0xF8) {
                // Clock, Start, Stop, etc. - ignore for now
                continue;
            }
            
            // System common messages
            if (byte >= 0xF0) {
                switch (byte) {
                    case 0xF0:  // SysEx start
                        midi->in_sysex = true;
                        midi->sysex_len = 0;
                        break;
                    case 0xF1:  // Time code
                    case 0xF3:  // Song select
                        midi->expected_count = 1;
                        midi->data_count = 0;
                        midi->running_status = 0;  // Clear running status
                        break;
                    case 0xF2:  // Song position
                        midi->expected_count = 2;
                        midi->data_count = 0;
                        midi->running_status = 0;
                        break;
                    default:
                        // F4, F5, F6, F7 (without SysEx) - ignore
                        break;
                }
                continue;
            }
            
            // Channel voice message - update running status
            midi->running_status = byte;
            midi->expected_count = midi_data_bytes_for_status(byte);
            midi->data_count = 0;
        } else {
            // Data byte
            if (midi->running_status == 0) {
                // No running status - ignore
                continue;
            }
            
            midi->data_bytes[midi->data_count++] = byte;
            
            if (midi->data_count >= midi->expected_count) {
                // Complete message - generate event
                uint8_t channel = midi->running_status & 0x0F;
                uint8_t type = midi->running_status & 0xF0;
                
                ext_ctrl_event_t event = {
                    .source = EXT_CTRL_SOURCE_MIDI,
                    .device_id = 0,
                    .timestamp = now,
                    .data.midi = {
                        .channel = channel,
                        .type = type,
                        .data1 = midi->data_bytes[0],
                        .data2 = (midi->expected_count > 1) ? midi->data_bytes[1] : 0
                    }
                };
                
                // Determine element type based on MIDI message
                switch (type) {
                    case 0x80:  // Note Off
                        event.element = EXT_CTRL_ELEM_PAD;
                        event.data.button.pressed = false;
                        event.data.button.velocity = 0;
                        event.element_id = midi->data_bytes[0];
                        break;
                        
                    case 0x90:  // Note On (velocity 0 = off)
                        event.element = EXT_CTRL_ELEM_PAD;
                        event.data.button.pressed = (midi->data_bytes[1] > 0);
                        event.data.button.velocity = midi->data_bytes[1];
                        event.element_id = midi->data_bytes[0];
                        break;
                        
                    case 0xB0:  // Control Change
                        if (midi->data_bytes[0] >= 64 && midi->data_bytes[0] <= 69) {
                            // Switches (on/off)
                            event.element = EXT_CTRL_ELEM_BUTTON;
                            event.data.button.pressed = (midi->data_bytes[1] >= 64);
                            event.data.button.velocity = midi->data_bytes[1];
                        } else if (midi->data_bytes[0] == 0x40) {
                            // Encoder (relative)
                            event.element = EXT_CTRL_ELEM_ENCODER;
                            // 7-bit signed: 1-63 = CW, 65-127 = CCW
                            int8_t delta = (midi->data_bytes[1] < 64) 
                                          ? midi->data_bytes[1] 
                                          : -(int8_t)(midi->data_bytes[1] - 64);
                            event.data.encoder.delta = delta;
                            event.data.encoder.acceleration = 1;
                        } else {
                            // Continuous controller (knob/fader)
                            event.element = EXT_CTRL_ELEM_KNOB;
                            event.data.analog.value = midi->data_bytes[1] << 7; // Scale to 14-bit
                            event.data.analog.delta = 0;
                        }
                        event.element_id = midi->data_bytes[0];
                        break;
                        
                    case 0xE0:  // Pitch Bend
                        event.element = EXT_CTRL_ELEM_FADER;
                        event.data.analog.value = midi->data_bytes[0] | (midi->data_bytes[1] << 7);
                        event.data.analog.delta = 0;
                        event.element_id = 128;  // Special ID for pitch bend
                        break;
                        
                    default:
                        // Other message types - skip
                        midi->data_count = 0;
                        continue;
                }
                
                queue_raw_event(&event);
                midi->data_count = 0;
            }
        }
    }
}

bool ext_ctrl_midi_send(uint8_t channel, ext_ctrl_midi_type_t type, 
                        uint8_t data1, uint8_t data2) {
    // TODO: Implement MIDI output via UART or USB
    ESP_LOGD(TAG, "MIDI TX: ch=%d type=0x%02X d1=%d d2=%d",
             channel, type, data1, data2);
    return false;
}

/* ============================================================================
 * USB HID Implementation
 * ============================================================================ */

#ifndef USB_HOST_DISABLE

// USB HID class codes
#define USB_CLASS_HID           0x03
#define USB_SUBCLASS_BOOT       0x01
#define USB_PROTOCOL_KEYBOARD   0x01
#define USB_PROTOCOL_MOUSE      0x02

static void hid_host_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg) {
    // Handle HID device connection/disconnection
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "HID device connected");
            // TODO: Enumerate device, check if HID, parse report descriptor
            break;
            
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "HID device disconnected");
            break;
    }
}

#endif

bool ext_ctrl_hid_start(void) {
#ifdef USB_HOST_DISABLE
    ESP_LOGW(TAG, "USB HID disabled (USB_HOST_DISABLE defined)");
    return false;
#else
    if (s_state.hid_running) {
        return true;
    }
    
    // USB HID implementation would go here
    // For now, this is a stub that relies on usb_host.c
    
    s_state.hid_running = true;
    ESP_LOGI(TAG, "USB HID host started");
    return true;
#endif
}

void ext_ctrl_hid_stop(void) {
    s_state.hid_running = false;
}

bool ext_ctrl_hid_get_device(uint8_t device_id, ext_ctrl_hid_device_t *info) {
    if (device_id >= s_state.hid_device_count || !info) {
        return false;
    }
    
    memcpy(info, &s_state.hid_devices[device_id], sizeof(ext_ctrl_hid_device_t));
    return true;
}

uint8_t ext_ctrl_hid_get_device_count(void) {
    return s_state.hid_device_count;
}

/* ============================================================================
 * Profile Management
 * ============================================================================ */

static bool ensure_nvs_open(void) {
    if (s_state.nvs_open) {
        return true;
    }
    
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_state.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    s_state.nvs_open = true;
    return true;
}

bool ext_ctrl_profile_load(uint8_t profile_id, ext_ctrl_profile_t *profile) {
    if (!ensure_nvs_open() || !profile || profile_id >= EXT_CTRL_MAX_PROFILES) {
        return false;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PROFILE_PREFIX, profile_id);
    
    size_t size = sizeof(ext_ctrl_profile_t);
    esp_err_t err = nvs_get_blob(s_state.nvs_handle, key, profile, &size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load profile %d: %s", profile_id, esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Loaded profile %d: '%s' (%d mappings)", 
             profile_id, profile->name, profile->mapping_count);
    return true;
}

bool ext_ctrl_profile_save(uint8_t profile_id, const ext_ctrl_profile_t *profile) {
    if (!ensure_nvs_open() || !profile || profile_id >= EXT_CTRL_MAX_PROFILES) {
        return false;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PROFILE_PREFIX, profile_id);
    
    esp_err_t err = nvs_set_blob(s_state.nvs_handle, key, profile, sizeof(ext_ctrl_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save profile %d: %s", profile_id, esp_err_to_name(err));
        return false;
    }
    
    err = nvs_commit(s_state.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Saved profile %d: '%s'", profile_id, profile->name);
    return true;
}

bool ext_ctrl_profile_delete(uint8_t profile_id) {
    if (!ensure_nvs_open() || profile_id >= EXT_CTRL_MAX_PROFILES) {
        return false;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PROFILE_PREFIX, profile_id);
    
    esp_err_t err = nvs_erase_key(s_state.nvs_handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    
    nvs_commit(s_state.nvs_handle);
    return true;
}

const ext_ctrl_profile_t* ext_ctrl_profile_get_active(void) {
    return s_state.profile_loaded ? &s_state.active_profile : NULL;
}

bool ext_ctrl_profile_activate(uint8_t profile_id) {
    ext_ctrl_profile_t profile;
    
    if (!ext_ctrl_profile_load(profile_id, &profile)) {
        return false;
    }
    
    return ext_ctrl_profile_apply(&profile);
}

bool ext_ctrl_profile_apply(const ext_ctrl_profile_t *profile) {
    if (!profile) {
        return false;
    }
    
    if (s_state.mutex) {
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    }
    
    memcpy(&s_state.active_profile, profile, sizeof(ext_ctrl_profile_t));
    s_state.profile_loaded = true;
    
    if (s_state.mutex) {
        xSemaphoreGive(s_state.mutex);
    }
    
    ESP_LOGI(TAG, "Applied profile: '%s' (%d mappings)", 
             profile->name, profile->mapping_count);
    return true;
}

bool ext_ctrl_profile_matches_device(const ext_ctrl_profile_t *profile,
                                     uint16_t vendor_id, uint16_t product_id,
                                     const char *midi_name) {
    if (!profile) {
        return false;
    }
    
    // Check USB VID/PID
    if (profile->vendor_id != 0 && profile->product_id != 0) {
        if (profile->vendor_id == vendor_id && profile->product_id == product_id) {
            return true;
        }
    }
    
    // Check MIDI device name
    if (midi_name && profile->midi_device_name[0] != '\0') {
        if (strstr(midi_name, profile->midi_device_name) != NULL) {
            return true;
        }
    }
    
    return false;
}

int ext_ctrl_profile_auto_select(void) {
    // Get connected device info
    ext_ctrl_hid_device_t hid_dev;
    uint16_t vid = 0, pid = 0;
    
    if (ext_ctrl_hid_get_device(0, &hid_dev) && hid_dev.connected) {
        vid = hid_dev.vendor_id;
        pid = hid_dev.product_id;
    }
    
    // Scan profiles for match
    for (int i = 0; i < EXT_CTRL_MAX_PROFILES; i++) {
        ext_ctrl_profile_t profile;
        if (ext_ctrl_profile_load(i, &profile)) {
            if (ext_ctrl_profile_matches_device(&profile, vid, pid, NULL)) {
                if (ext_ctrl_profile_apply(&profile)) {
                    ESP_LOGI(TAG, "Auto-selected profile %d: '%s'", i, profile.name);
                    return i;
                }
            }
        }
    }
    
    return -1;
}

/* ============================================================================
 * Learn Mode
 * ============================================================================ */

bool ext_ctrl_learn_start(ext_ctrl_action_t action, uint8_t target_deck,
                          ext_ctrl_learn_cb_t callback, void *arg) {
    if (s_state.learn.active) {
        ESP_LOGW(TAG, "Learn mode already active");
        return false;
    }
    
    s_state.learn.active = true;
    s_state.learn.target_action = action;
    s_state.learn.target_deck = target_deck;
    s_state.learn.callback = callback;
    s_state.learn.callback_arg = arg;
    s_state.learn.input_captured = false;
    memset(&s_state.learn.captured_input, 0, sizeof(ext_ctrl_input_id_t));
    
    ESP_LOGI(TAG, "Learn mode started for action: %s (deck %d)",
             ext_ctrl_action_name(action), target_deck);
    
    return true;
}

bool ext_ctrl_learn_stop(bool save) {
    if (!s_state.learn.active) {
        return false;
    }
    
    bool result = false;
    
    if (save && s_state.learn.input_captured) {
        // Create mapping
        ext_ctrl_mapping_t mapping = {
            .input = s_state.learn.captured_input,
            .action = s_state.learn.target_action,
            .shift_action = EXT_ACTION_NONE,
            .target_deck = s_state.learn.target_deck,
            .invert = false,
            .min_value = 0,
            .max_value = 127,
            .sensitivity = 1,
            .enabled = true
        };
        
        result = ext_ctrl_mapping_add(&mapping);
        
        if (result) {
            ESP_LOGI(TAG, "Mapping saved: %s -> %s",
                     ext_ctrl_source_name(mapping.input.source),
                     ext_ctrl_action_name(mapping.action));
        }
    }
    
    s_state.learn.active = false;
    ESP_LOGI(TAG, "Learn mode stopped (saved: %s)", result ? "yes" : "no");
    
    return result;
}

bool ext_ctrl_learn_is_active(void) {
    return s_state.learn.active;
}

bool ext_ctrl_learn_get_captured(ext_ctrl_input_id_t *input) {
    if (!input || !s_state.learn.input_captured) {
        return false;
    }
    
    memcpy(input, &s_state.learn.captured_input, sizeof(ext_ctrl_input_id_t));
    return true;
}

int ext_ctrl_mapping_clear(ext_ctrl_action_t action, uint8_t deck) {
    if (!s_state.profile_loaded) {
        return 0;
    }
    
    int cleared = 0;
    ext_ctrl_profile_t *profile = &s_state.active_profile;
    
    for (int i = profile->mapping_count - 1; i >= 0; i--) {
        ext_ctrl_mapping_t *m = &profile->mappings[i];
        
        if (m->action == action && (deck == 0xFF || m->target_deck == deck)) {
            // Remove by shifting remaining mappings
            for (int j = i; j < profile->mapping_count - 1; j++) {
                profile->mappings[j] = profile->mappings[j + 1];
            }
            profile->mapping_count--;
            cleared++;
        }
    }
    
    return cleared;
}

bool ext_ctrl_mapping_add(const ext_ctrl_mapping_t *mapping) {
    if (!mapping || !s_state.profile_loaded) {
        return false;
    }
    
    ext_ctrl_profile_t *profile = &s_state.active_profile;
    
    if (profile->mapping_count >= EXT_CTRL_MAX_MAPPINGS) {
        ESP_LOGE(TAG, "Maximum mappings reached");
        return false;
    }
    
    memcpy(&profile->mappings[profile->mapping_count], mapping, sizeof(ext_ctrl_mapping_t));
    profile->mapping_count++;
    
    return true;
}

/* ============================================================================
 * Event Queue Interface
 * ============================================================================ */

bool ext_ctrl_event_get(ext_ctrl_event_t *event) {
    if (!event || !s_state.raw_event_queue) {
        return false;
    }
    
    return xQueueReceive(s_state.raw_event_queue, event, 0) == pdTRUE;
}

bool ext_ctrl_action_get(ext_ctrl_action_event_t *event) {
    if (!event || !s_state.action_event_queue) {
        return false;
    }
    
    return xQueueReceive(s_state.action_event_queue, event, 0) == pdTRUE;
}

void ext_ctrl_set_raw_callback(ext_ctrl_raw_cb_t callback, void *arg) {
    s_state.raw_callback = callback;
    s_state.raw_callback_arg = arg;
}

void ext_ctrl_set_action_callback(ext_ctrl_action_cb_t callback, void *arg) {
    s_state.action_callback = callback;
    s_state.action_callback_arg = arg;
}

void ext_ctrl_update(void) {
    // Update encoders
    ext_ctrl_encoder_update();
    
    // Update matrix
    ext_ctrl_matrix_scan();
    
    // Process raw events
    ext_ctrl_event_t raw_event;
    while (ext_ctrl_event_get(&raw_event)) {
        // Invoke raw callback
        if (s_state.raw_callback) {
            s_state.raw_callback(&raw_event, s_state.raw_callback_arg);
        }
        
        // Process through mapping
        process_event_mapping(&raw_event);
    }
    
    // Process action events
    ext_ctrl_action_event_t action_event;
    while (ext_ctrl_action_get(&action_event)) {
        if (s_state.action_callback) {
            s_state.action_callback(&action_event, s_state.action_callback_arg);
        }
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char* ext_ctrl_action_name(ext_ctrl_action_t action) {
    if (action < EXT_ACTION_COUNT) {
        return ACTION_NAMES[action];
    }
    return "Unknown";
}

const char* ext_ctrl_source_name(ext_ctrl_source_t source) {
    if (source <= EXT_CTRL_SOURCE_ANALOG) {
        return SOURCE_NAMES[source];
    }
    return "Unknown";
}

const char* ext_ctrl_element_name(ext_ctrl_element_t element) {
    if (element <= EXT_CTRL_ELEM_PAD) {
        return ELEMENT_NAMES[element];
    }
    return "Unknown";
}

void ext_ctrl_get_stats(uint32_t *events_received, uint32_t *events_mapped,
                        uint32_t *events_dropped) {
    if (events_received) *events_received = s_state.events_received;
    if (events_mapped) *events_mapped = s_state.events_mapped;
    if (events_dropped) *events_dropped = s_state.events_dropped;
}

void ext_ctrl_reset_stats(void) {
    s_state.events_received = 0;
    s_state.events_mapped = 0;
    s_state.events_dropped = 0;
}

/* ============================================================================
 * Factory Profiles
 * ============================================================================ */

// Generic DJ controller profile
static const ext_ctrl_mapping_t GENERIC_DJ_MAPPINGS[] = {
    // Play/Pause (MIDI Note 0x3B on channel 0)
    {
        .input = { .source = EXT_CTRL_SOURCE_MIDI, .element = EXT_CTRL_ELEM_PAD,
                   .midi_channel = 0, .midi_type = 0x90, .midi_note_cc = 0x3B },
        .action = EXT_ACTION_PLAY_PAUSE,
        .target_deck = 0,
        .enabled = true
    },
    // Cue (MIDI Note 0x3C on channel 0)
    {
        .input = { .source = EXT_CTRL_SOURCE_MIDI, .element = EXT_CTRL_ELEM_PAD,
                   .midi_channel = 0, .midi_type = 0x90, .midi_note_cc = 0x3C },
        .action = EXT_ACTION_CUE,
        .target_deck = 0,
        .enabled = true
    },
    // Sync (MIDI Note 0x3D on channel 0)
    {
        .input = { .source = EXT_CTRL_SOURCE_MIDI, .element = EXT_CTRL_ELEM_PAD,
                   .midi_channel = 0, .midi_type = 0x90, .midi_note_cc = 0x3D },
        .action = EXT_ACTION_SYNC,
        .target_deck = 0,
        .enabled = true
    },
    // Pitch fader (MIDI CC 0x00 on channel 0)
    {
        .input = { .source = EXT_CTRL_SOURCE_MIDI, .element = EXT_CTRL_ELEM_FADER,
                   .midi_channel = 0, .midi_type = 0xB0, .midi_note_cc = 0x00 },
        .action = EXT_ACTION_PITCH_FADER,
        .target_deck = 0,
        .min_value = 0,
        .max_value = 16383,
        .enabled = true
    },
};

static const char* FACTORY_PROFILE_NAMES[] = {
    "Generic DJ",
    "Generic MIDI",
    NULL
};

bool ext_ctrl_factory_profile_get(ext_ctrl_profile_t *profile, const char *profile_name) {
    if (!profile || !profile_name) {
        return false;
    }
    
    memset(profile, 0, sizeof(ext_ctrl_profile_t));
    
    if (strcmp(profile_name, "Generic DJ") == 0) {
        strncpy(profile->name, "Generic DJ", EXT_CTRL_PROFILE_NAME_LEN - 1);
        profile->is_factory = true;
        profile->version = 1;
        
        // Copy mappings
        profile->mapping_count = sizeof(GENERIC_DJ_MAPPINGS) / sizeof(GENERIC_DJ_MAPPINGS[0]);
        memcpy(profile->mappings, GENERIC_DJ_MAPPINGS, sizeof(GENERIC_DJ_MAPPINGS));
        
        return true;
    }
    
    if (strcmp(profile_name, "Generic MIDI") == 0) {
        strncpy(profile->name, "Generic MIDI", EXT_CTRL_PROFILE_NAME_LEN - 1);
        profile->is_factory = true;
        profile->version = 1;
        profile->mapping_count = 0;
        
        return true;
    }
    
    return false;
}

int ext_ctrl_factory_profile_list(const char **names, int max_names) {
    int count = 0;
    
    for (int i = 0; FACTORY_PROFILE_NAMES[i] != NULL && count < max_names; i++) {
        if (names) {
            names[count] = FACTORY_PROFILE_NAMES[i];
        }
        count++;
    }
    
    return count;
}

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================ */

bool ext_ctrl_init(void) {
    if (s_state.initialized) {
        ESP_LOGW(TAG, "External controller already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing external controller system");
    
    // Create mutex
    s_state.mutex = xSemaphoreCreateMutex();
    if (!s_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Create event queues
    s_state.raw_event_queue = xQueueCreate(EXT_CTRL_EVENT_QUEUE_SIZE, 
                                            sizeof(ext_ctrl_event_t));
    s_state.action_event_queue = xQueueCreate(EXT_CTRL_EVENT_QUEUE_SIZE,
                                               sizeof(ext_ctrl_action_event_t));
    
    if (!s_state.raw_event_queue || !s_state.action_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queues");
        ext_ctrl_deinit();
        return false;
    }
    
    // Initialize subsystems
    memset(s_state.encoders, 0, sizeof(s_state.encoders));
    memset(&s_state.matrix, 0, sizeof(s_state.matrix));
    memset(&s_state.midi, 0, sizeof(s_state.midi));
    memset(&s_state.learn, 0, sizeof(s_state.learn));
    memset(&s_state.active_profile, 0, sizeof(s_state.active_profile));
    
    s_state.profile_loaded = false;
    s_state.shift_held = false;
    s_state.events_received = 0;
    s_state.events_mapped = 0;
    s_state.events_dropped = 0;
    
    // Try to load default/last used profile
    if (ensure_nvs_open()) {
        uint8_t last_profile = 0;
        size_t size = sizeof(last_profile);
        if (nvs_get_u8(s_state.nvs_handle, NVS_KEY_ACTIVE_PROFILE, &last_profile) == ESP_OK) {
            ext_ctrl_profile_activate(last_profile);
        }
    }
    
    // If no profile loaded, try factory default
    if (!s_state.profile_loaded) {
        ext_ctrl_profile_t factory;
        if (ext_ctrl_factory_profile_get(&factory, "Generic DJ")) {
            ext_ctrl_profile_apply(&factory);
        }
    }
    
    s_state.initialized = true;
    ESP_LOGI(TAG, "External controller system initialized");
    
    return true;
}

void ext_ctrl_deinit(void) {
    if (!s_state.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing external controller system");
    
    // Stop subsystems
    ext_ctrl_hid_stop();
    ext_ctrl_midi_disable();
    ext_ctrl_matrix_deinit();
    
    // Deinit encoders
    for (int i = 0; i < EXT_CTRL_MAX_ENCODERS; i++) {
        ext_ctrl_encoder_deinit(i);
    }
    
    // Close NVS
    if (s_state.nvs_open) {
        nvs_close(s_state.nvs_handle);
        s_state.nvs_open = false;
    }
    
    // Delete queues
    if (s_state.raw_event_queue) {
        vQueueDelete(s_state.raw_event_queue);
        s_state.raw_event_queue = NULL;
    }
    
    if (s_state.action_event_queue) {
        vQueueDelete(s_state.action_event_queue);
        s_state.action_event_queue = NULL;
    }
    
    // Delete mutex
    if (s_state.mutex) {
        vSemaphoreDelete(s_state.mutex);
        s_state.mutex = NULL;
    }
    
    s_state.initialized = false;
    ESP_LOGI(TAG, "External controller system deinitialized");
}

bool ext_ctrl_is_initialized(void) {
    return s_state.initialized;
}
