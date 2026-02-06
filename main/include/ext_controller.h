/**
 * @file ext_controller.h
 * @brief External controller support for USB HID, MIDI, and button matrices
 * 
 * Provides unified input handling for:
 * - USB HID controllers (joysticks, gamepads, DJ controllers)
 * - MIDI controllers (with CC, note, and sysex mapping)
 * - Button matrix scanning (4x4 up to 8x8)
 * - Rotary encoders with acceleration
 * 
 * Features:
 * - Controller profile system with NVS persistence
 * - Learn mode for custom mapping
 * - Event queue for async input handling
 * - Debouncing and filtering
 */

#ifndef EXT_CONTROLLER_H
#define EXT_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Limits
 * ============================================================================ */

#define EXT_CTRL_MAX_BUTTONS        64      ///< Maximum buttons per controller
#define EXT_CTRL_MAX_ENCODERS       16      ///< Maximum rotary encoders
#define EXT_CTRL_MAX_AXES           8       ///< Maximum analog axes
#define EXT_CTRL_MAX_MAPPINGS       128     ///< Maximum mappings in a profile
#define EXT_CTRL_MAX_PROFILES       8       ///< Maximum stored profiles
#define EXT_CTRL_EVENT_QUEUE_SIZE   32      ///< Event queue capacity
#define EXT_CTRL_PROFILE_NAME_LEN   32      ///< Profile name length

#define EXT_CTRL_MATRIX_MAX_ROWS    8       ///< Maximum matrix rows
#define EXT_CTRL_MATRIX_MAX_COLS    8       ///< Maximum matrix columns

#define EXT_CTRL_DEBOUNCE_MS        20      ///< Default button debounce time
#define EXT_CTRL_ENCODER_ACCEL_MS   50      ///< Encoder acceleration threshold

/* ============================================================================
 * Controller Types
 * ============================================================================ */

/**
 * @brief Controller input source types
 */
typedef enum {
    EXT_CTRL_SOURCE_NONE = 0,
    EXT_CTRL_SOURCE_USB_HID,        ///< USB HID device
    EXT_CTRL_SOURCE_MIDI,           ///< MIDI controller
    EXT_CTRL_SOURCE_MATRIX,         ///< Button matrix (GPIO)
    EXT_CTRL_SOURCE_ENCODER,        ///< Rotary encoder (GPIO)
    EXT_CTRL_SOURCE_ANALOG          ///< Analog input (ADC)
} ext_ctrl_source_t;

/**
 * @brief Input element types
 */
typedef enum {
    EXT_CTRL_ELEM_BUTTON = 0,       ///< Momentary button/switch
    EXT_CTRL_ELEM_TOGGLE,           ///< Toggle switch (latching)
    EXT_CTRL_ELEM_ENCODER,          ///< Rotary encoder (relative)
    EXT_CTRL_ELEM_FADER,            ///< Linear fader/slider
    EXT_CTRL_ELEM_KNOB,             ///< Potentiometer/knob
    EXT_CTRL_ELEM_JOGWHEEL,         ///< Jog wheel (high-resolution encoder)
    EXT_CTRL_ELEM_PAD               ///< Velocity-sensitive pad
} ext_ctrl_element_t;

/**
 * @brief MIDI message types for mapping
 */
typedef enum {
    EXT_CTRL_MIDI_NOTE_ON = 0x90,
    EXT_CTRL_MIDI_NOTE_OFF = 0x80,
    EXT_CTRL_MIDI_CC = 0xB0,        ///< Control Change
    EXT_CTRL_MIDI_PITCH = 0xE0,     ///< Pitch bend
    EXT_CTRL_MIDI_AFTERTOUCH = 0xD0,
    EXT_CTRL_MIDI_SYSEX = 0xF0
} ext_ctrl_midi_type_t;

/* ============================================================================
 * DJ Action Targets
 * ============================================================================ */

/**
 * @brief DJ deck actions that can be mapped
 */
typedef enum {
    // Playback control
    EXT_ACTION_NONE = 0,
    EXT_ACTION_PLAY,
    EXT_ACTION_PAUSE,
    EXT_ACTION_PLAY_PAUSE,
    EXT_ACTION_CUE,
    EXT_ACTION_CUE_PLAY,
    EXT_ACTION_SYNC,
    EXT_ACTION_SYNC_TOGGLE,
    
    // Hot cues (1-8)
    EXT_ACTION_HOT_CUE_1,
    EXT_ACTION_HOT_CUE_2,
    EXT_ACTION_HOT_CUE_3,
    EXT_ACTION_HOT_CUE_4,
    EXT_ACTION_HOT_CUE_5,
    EXT_ACTION_HOT_CUE_6,
    EXT_ACTION_HOT_CUE_7,
    EXT_ACTION_HOT_CUE_8,
    
    // Loop control
    EXT_ACTION_LOOP_IN,
    EXT_ACTION_LOOP_OUT,
    EXT_ACTION_LOOP_TOGGLE,
    EXT_ACTION_LOOP_DOUBLE,
    EXT_ACTION_LOOP_HALVE,
    EXT_ACTION_LOOP_ROLL_1_16,
    EXT_ACTION_LOOP_ROLL_1_8,
    EXT_ACTION_LOOP_ROLL_1_4,
    EXT_ACTION_LOOP_ROLL_1_2,
    EXT_ACTION_LOOP_ROLL_1,
    EXT_ACTION_LOOP_ROLL_2,
    EXT_ACTION_LOOP_ROLL_4,
    
    // Jog/scratch
    EXT_ACTION_JOG_TOUCH,
    EXT_ACTION_JOG_TURN,
    EXT_ACTION_JOG_SCRATCH,
    EXT_ACTION_SLIP_MODE,
    
    // Pitch/tempo
    EXT_ACTION_PITCH_FADER,
    EXT_ACTION_PITCH_BEND_PLUS,
    EXT_ACTION_PITCH_BEND_MINUS,
    EXT_ACTION_TEMPO_UP,
    EXT_ACTION_TEMPO_DOWN,
    EXT_ACTION_KEY_LOCK,
    
    // EQ/Filter
    EXT_ACTION_EQ_LOW,
    EXT_ACTION_EQ_MID,
    EXT_ACTION_EQ_HIGH,
    EXT_ACTION_FILTER,
    
    // Effects
    EXT_ACTION_FX_1,
    EXT_ACTION_FX_2,
    EXT_ACTION_FX_3,
    EXT_ACTION_FX_WET_DRY,
    EXT_ACTION_FX_PARAM_1,
    EXT_ACTION_FX_PARAM_2,
    
    // Navigation
    EXT_ACTION_TRACK_NEXT,
    EXT_ACTION_TRACK_PREV,
    EXT_ACTION_LOAD_TRACK,
    EXT_ACTION_EJECT,
    EXT_ACTION_BROWSE_UP,
    EXT_ACTION_BROWSE_DOWN,
    EXT_ACTION_BROWSE_SELECT,
    EXT_ACTION_BROWSE_BACK,
    
    // Mixer (multi-deck)
    EXT_ACTION_CROSSFADER,
    EXT_ACTION_CHANNEL_FADER,
    EXT_ACTION_MASTER_VOLUME,
    EXT_ACTION_CUE_MIX,
    EXT_ACTION_HEADPHONE_VOLUME,
    
    // Mode switches
    EXT_ACTION_SHIFT,
    EXT_ACTION_DECK_SELECT,
    EXT_ACTION_VINYL_MODE,
    
    EXT_ACTION_COUNT
} ext_ctrl_action_t;

/* ============================================================================
 * Event Structures
 * ============================================================================ */

/**
 * @brief Raw input event from any controller source
 */
typedef struct {
    ext_ctrl_source_t source;       ///< Input source type
    ext_ctrl_element_t element;     ///< Element type
    uint8_t device_id;              ///< Device/controller index
    uint8_t element_id;             ///< Button/encoder/axis index
    
    union {
        struct {
            bool pressed;           ///< Button state
            uint8_t velocity;       ///< Velocity (pads), 0-127
        } button;
        
        struct {
            int16_t delta;          ///< Relative movement
            uint8_t acceleration;   ///< Acceleration factor (1-8)
        } encoder;
        
        struct {
            uint16_t value;         ///< Absolute position (0-16383 for MIDI)
            int16_t delta;          ///< Change since last update
        } analog;
        
        struct {
            uint8_t channel;        ///< MIDI channel (0-15)
            uint8_t type;           ///< MIDI message type
            uint8_t data1;          ///< First data byte (note/CC number)
            uint8_t data2;          ///< Second data byte (velocity/value)
        } midi;
    } data;
    
    uint32_t timestamp;             ///< Event timestamp (ms)
} ext_ctrl_event_t;

/**
 * @brief Processed action event (after mapping)
 */
typedef struct {
    ext_ctrl_action_t action;       ///< Mapped action
    uint8_t deck;                   ///< Target deck (0=A, 1=B, 2=global)
    
    union {
        bool pressed;               ///< Button pressed state
        int16_t relative;           ///< Relative value (encoder delta)
        uint16_t absolute;          ///< Absolute value (0-16383)
        float normalized;           ///< Normalized value (0.0-1.0)
    } value;
    
    bool shift_held;                ///< Shift modifier was held
    uint32_t timestamp;
} ext_ctrl_action_event_t;

/* ============================================================================
 * Mapping Configuration
 * ============================================================================ */

/**
 * @brief Input source identifier for mapping
 */
typedef struct {
    ext_ctrl_source_t source;
    uint8_t device_id;
    ext_ctrl_element_t element;
    uint8_t element_id;
    
    // MIDI-specific
    uint8_t midi_channel;           ///< 0-15, or 0xFF for any channel
    uint8_t midi_type;              ///< MIDI message type
    uint8_t midi_note_cc;           ///< Note number or CC number
    
    // HID-specific
    uint16_t hid_usage_page;
    uint16_t hid_usage;
} ext_ctrl_input_id_t;

/**
 * @brief Single input-to-action mapping
 */
typedef struct {
    ext_ctrl_input_id_t input;      ///< Input identifier
    ext_ctrl_action_t action;       ///< Target action
    ext_ctrl_action_t shift_action; ///< Action when shift held (or NONE)
    uint8_t target_deck;            ///< 0=A, 1=B, 2=both, 3=active
    
    // Value transformation
    bool invert;                    ///< Invert value
    uint16_t min_value;             ///< Input range minimum
    uint16_t max_value;             ///< Input range maximum
    uint8_t sensitivity;            ///< Sensitivity multiplier (1-10)
    
    // Flags
    bool enabled;
} ext_ctrl_mapping_t;

/**
 * @brief Controller profile (complete mapping configuration)
 */
typedef struct {
    char name[EXT_CTRL_PROFILE_NAME_LEN];
    uint16_t vendor_id;             ///< USB VID (0 = any)
    uint16_t product_id;            ///< USB PID (0 = any)
    char midi_device_name[32];      ///< MIDI device name pattern
    
    ext_ctrl_mapping_t mappings[EXT_CTRL_MAX_MAPPINGS];
    uint16_t mapping_count;
    
    bool is_factory;                ///< Factory preset (read-only)
    uint32_t version;               ///< Profile version
} ext_ctrl_profile_t;

/* ============================================================================
 * USB HID Configuration
 * ============================================================================ */

/**
 * @brief USB HID device info
 */
typedef struct {
    bool connected;
    uint16_t vendor_id;
    uint16_t product_id;
    char manufacturer[64];
    char product[64];
    uint8_t report_size;
    uint8_t device_id;
} ext_ctrl_hid_device_t;

/* ============================================================================
 * Button Matrix Configuration
 * ============================================================================ */

/**
 * @brief Button matrix GPIO configuration
 */
typedef struct {
    uint8_t row_pins[EXT_CTRL_MATRIX_MAX_ROWS];
    uint8_t col_pins[EXT_CTRL_MATRIX_MAX_COLS];
    uint8_t num_rows;
    uint8_t num_cols;
    bool active_low;                ///< Buttons pull low when pressed
    uint16_t debounce_ms;
} ext_ctrl_matrix_config_t;

/* ============================================================================
 * Rotary Encoder Configuration
 * ============================================================================ */

/**
 * @brief Rotary encoder GPIO configuration
 */
typedef struct {
    int8_t pin_a;                   ///< Encoder A pin (-1 if unused)
    int8_t pin_b;                   ///< Encoder B pin
    int8_t pin_button;              ///< Push button pin (-1 if none)
    uint8_t pulses_per_detent;      ///< Pulses per detent (1, 2, or 4)
    bool reverse_direction;
    
    // Acceleration settings
    bool acceleration_enabled;
    uint16_t accel_threshold_ms;    ///< Time threshold for acceleration
    uint8_t accel_max_multiplier;   ///< Maximum acceleration (1-8)
} ext_ctrl_encoder_config_t;

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * @brief Raw event callback (before mapping)
 */
typedef void (*ext_ctrl_raw_cb_t)(const ext_ctrl_event_t *event, void *arg);

/**
 * @brief Action event callback (after mapping)
 */
typedef void (*ext_ctrl_action_cb_t)(const ext_ctrl_action_event_t *event, void *arg);

/**
 * @brief Learn mode callback (for UI feedback)
 */
typedef void (*ext_ctrl_learn_cb_t)(const ext_ctrl_input_id_t *input, void *arg);

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================ */

/**
 * @brief Initialize external controller system
 * 
 * Sets up USB HID host, MIDI parsing, and event queue.
 * Call after USB host is initialized.
 * 
 * @return true on success, false on failure
 */
bool ext_ctrl_init(void);

/**
 * @brief Deinitialize external controller system
 */
void ext_ctrl_deinit(void);

/**
 * @brief Check if external controller system is initialized
 */
bool ext_ctrl_is_initialized(void);

/* ============================================================================
 * USB HID Interface
 * ============================================================================ */

/**
 * @brief Start USB HID host scanning
 * 
 * Begins monitoring for USB HID controllers.
 * 
 * @return true on success
 */
bool ext_ctrl_hid_start(void);

/**
 * @brief Stop USB HID host
 */
void ext_ctrl_hid_stop(void);

/**
 * @brief Get connected HID device info
 * 
 * @param device_id Device index (0-based)
 * @param info Output device info
 * @return true if device exists
 */
bool ext_ctrl_hid_get_device(uint8_t device_id, ext_ctrl_hid_device_t *info);

/**
 * @brief Get number of connected HID devices
 */
uint8_t ext_ctrl_hid_get_device_count(void);

/* ============================================================================
 * MIDI Interface
 * ============================================================================ */

/**
 * @brief Enable MIDI input processing
 * 
 * @param uart_num UART number for MIDI input (0-2), or -1 for USB MIDI
 * @return true on success
 */
bool ext_ctrl_midi_enable(int uart_num);

/**
 * @brief Disable MIDI input
 */
void ext_ctrl_midi_disable(void);

/**
 * @brief Process incoming MIDI data
 * 
 * Call from MIDI receive task/interrupt with raw bytes.
 * 
 * @param data MIDI byte(s)
 * @param len Number of bytes
 */
void ext_ctrl_midi_process(const uint8_t *data, size_t len);

/**
 * @brief Send MIDI message (for LED feedback, etc.)
 * 
 * @param channel MIDI channel (0-15)
 * @param type Message type
 * @param data1 First data byte
 * @param data2 Second data byte
 * @return true on success
 */
bool ext_ctrl_midi_send(uint8_t channel, ext_ctrl_midi_type_t type, 
                        uint8_t data1, uint8_t data2);

/* ============================================================================
 * Button Matrix Interface
 * ============================================================================ */

/**
 * @brief Configure and start button matrix scanning
 * 
 * @param config Matrix configuration
 * @return true on success
 */
bool ext_ctrl_matrix_init(const ext_ctrl_matrix_config_t *config);

/**
 * @brief Stop button matrix scanning
 */
void ext_ctrl_matrix_deinit(void);

/**
 * @brief Get button state from matrix
 * 
 * @param row Row index
 * @param col Column index
 * @return true if pressed
 */
bool ext_ctrl_matrix_get_button(uint8_t row, uint8_t col);

/**
 * @brief Scan matrix manually (if not using task)
 * 
 * Call periodically (1-5ms) for responsive scanning.
 */
void ext_ctrl_matrix_scan(void);

/* ============================================================================
 * Rotary Encoder Interface
 * ============================================================================ */

/**
 * @brief Configure a rotary encoder
 * 
 * @param encoder_id Encoder index (0 to EXT_CTRL_MAX_ENCODERS-1)
 * @param config Encoder configuration
 * @return true on success
 */
bool ext_ctrl_encoder_init(uint8_t encoder_id, const ext_ctrl_encoder_config_t *config);

/**
 * @brief Remove encoder configuration
 * 
 * @param encoder_id Encoder index
 */
void ext_ctrl_encoder_deinit(uint8_t encoder_id);

/**
 * @brief Get encoder delta (and clear)
 * 
 * Returns accumulated rotation since last call, with acceleration applied.
 * 
 * @param encoder_id Encoder index
 * @return Rotation delta (negative = CCW, positive = CW)
 */
int16_t ext_ctrl_encoder_get_delta(uint8_t encoder_id);

/**
 * @brief Get encoder button state
 * 
 * @param encoder_id Encoder index
 * @return true if button pressed
 */
bool ext_ctrl_encoder_get_button(uint8_t encoder_id);

/**
 * @brief Update all encoders (call from timer ISR or fast task)
 */
void ext_ctrl_encoder_update(void);

/* ============================================================================
 * Event Queue Interface
 * ============================================================================ */

/**
 * @brief Get next event from queue (non-blocking)
 * 
 * @param event Output event
 * @return true if event available, false if queue empty
 */
bool ext_ctrl_event_get(ext_ctrl_event_t *event);

/**
 * @brief Get next action event from queue (non-blocking)
 * 
 * Returns mapped action events (raw events run through profile mapping).
 * 
 * @param event Output action event
 * @return true if event available
 */
bool ext_ctrl_action_get(ext_ctrl_action_event_t *event);

/**
 * @brief Set raw event callback
 * 
 * Callback is invoked for every raw input event.
 * 
 * @param callback Callback function (NULL to disable)
 * @param arg User argument
 */
void ext_ctrl_set_raw_callback(ext_ctrl_raw_cb_t callback, void *arg);

/**
 * @brief Set action event callback
 * 
 * Callback is invoked for mapped action events.
 * 
 * @param callback Callback function (NULL to disable)
 * @param arg User argument
 */
void ext_ctrl_set_action_callback(ext_ctrl_action_cb_t callback, void *arg);

/**
 * @brief Process pending events
 * 
 * Call periodically from main loop to process event queue and
 * invoke callbacks. Also updates shift state.
 */
void ext_ctrl_update(void);

/* ============================================================================
 * Profile Management
 * ============================================================================ */

/**
 * @brief Load profile from NVS
 * 
 * @param profile_id Profile slot (0 to EXT_CTRL_MAX_PROFILES-1)
 * @param profile Output profile data
 * @return true if profile exists and was loaded
 */
bool ext_ctrl_profile_load(uint8_t profile_id, ext_ctrl_profile_t *profile);

/**
 * @brief Save profile to NVS
 * 
 * @param profile_id Profile slot
 * @param profile Profile data to save
 * @return true on success
 */
bool ext_ctrl_profile_save(uint8_t profile_id, const ext_ctrl_profile_t *profile);

/**
 * @brief Delete profile from NVS
 * 
 * @param profile_id Profile slot
 * @return true on success
 */
bool ext_ctrl_profile_delete(uint8_t profile_id);

/**
 * @brief Get active profile
 * 
 * @return Pointer to current active profile (read-only)
 */
const ext_ctrl_profile_t* ext_ctrl_profile_get_active(void);

/**
 * @brief Set active profile by ID
 * 
 * Loads and activates the specified profile.
 * 
 * @param profile_id Profile slot
 * @return true on success
 */
bool ext_ctrl_profile_activate(uint8_t profile_id);

/**
 * @brief Apply profile directly (without saving)
 * 
 * @param profile Profile to apply
 * @return true on success
 */
bool ext_ctrl_profile_apply(const ext_ctrl_profile_t *profile);

/**
 * @brief Check if device matches profile
 * 
 * @param profile Profile to check
 * @param vendor_id USB VID
 * @param product_id USB PID
 * @param midi_name MIDI device name (or NULL)
 * @return true if device matches profile criteria
 */
bool ext_ctrl_profile_matches_device(const ext_ctrl_profile_t *profile,
                                     uint16_t vendor_id, uint16_t product_id,
                                     const char *midi_name);

/**
 * @brief Auto-select profile for connected device
 * 
 * Scans profiles and activates matching one.
 * 
 * @return Profile ID that was activated, or -1 if no match
 */
int ext_ctrl_profile_auto_select(void);

/* ============================================================================
 * Learn Mode
 * ============================================================================ */

/**
 * @brief Enter learn mode
 * 
 * In learn mode, input events are captured for mapping.
 * 
 * @param action Action to map
 * @param target_deck Target deck (0=A, 1=B, 2=both)
 * @param callback Callback for captured input
 * @param arg User argument
 * @return true on success
 */
bool ext_ctrl_learn_start(ext_ctrl_action_t action, uint8_t target_deck,
                          ext_ctrl_learn_cb_t callback, void *arg);

/**
 * @brief Exit learn mode
 * 
 * @param save If true, save the captured mapping
 * @return true if mapping was saved
 */
bool ext_ctrl_learn_stop(bool save);

/**
 * @brief Check if in learn mode
 */
bool ext_ctrl_learn_is_active(void);

/**
 * @brief Get last captured input in learn mode
 * 
 * @param input Output input identifier
 * @return true if input was captured
 */
bool ext_ctrl_learn_get_captured(ext_ctrl_input_id_t *input);

/**
 * @brief Clear mapping for an action
 * 
 * @param action Action to unmap
 * @param deck Specific deck, or 0xFF for all
 * @return Number of mappings removed
 */
int ext_ctrl_mapping_clear(ext_ctrl_action_t action, uint8_t deck);

/**
 * @brief Add a mapping to active profile
 * 
 * @param mapping Mapping to add
 * @return true on success
 */
bool ext_ctrl_mapping_add(const ext_ctrl_mapping_t *mapping);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get action name string
 * 
 * @param action Action enum
 * @return Human-readable name
 */
const char* ext_ctrl_action_name(ext_ctrl_action_t action);

/**
 * @brief Get source name string
 * 
 * @param source Source enum
 * @return Human-readable name
 */
const char* ext_ctrl_source_name(ext_ctrl_source_t source);

/**
 * @brief Get element type name string
 * 
 * @param element Element enum
 * @return Human-readable name
 */
const char* ext_ctrl_element_name(ext_ctrl_element_t element);

/**
 * @brief Get statistics
 * 
 * @param events_received Total events received
 * @param events_mapped Events that matched a mapping
 * @param events_dropped Events dropped (queue full)
 */
void ext_ctrl_get_stats(uint32_t *events_received, uint32_t *events_mapped,
                        uint32_t *events_dropped);

/**
 * @brief Reset statistics
 */
void ext_ctrl_reset_stats(void);

/* ============================================================================
 * Factory Profiles
 * ============================================================================ */

/**
 * @brief Load factory default profile
 * 
 * @param profile Output profile
 * @param profile_name Profile name (e.g., "Generic DJ", "Pioneer DDJ")
 * @return true if found
 */
bool ext_ctrl_factory_profile_get(ext_ctrl_profile_t *profile, const char *profile_name);

/**
 * @brief Get list of available factory profiles
 * 
 * @param names Output array of profile names
 * @param max_names Maximum names to return
 * @return Number of profiles available
 */
int ext_ctrl_factory_profile_list(const char **names, int max_names);

#ifdef __cplusplus
}
#endif

#endif /* EXT_CONTROLLER_H */
