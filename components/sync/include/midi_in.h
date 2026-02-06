/**
 * @file midi_in.h
 * @brief MIDI input receiver with CC mapping and clock sync
 * 
 * Provides MIDI input via UART with:
 * - MIDI clock reception and BPM detection
 * - CC message parsing with deck control mapping
 * - Transport message handling (start/stop/continue)
 * - Note on/off events for trigger-based controls
 */

#ifndef MIDI_IN_H
#define MIDI_IN_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

/** MIDI message types (status byte high nibble) */
#define MIDI_STATUS_NOTE_OFF        0x80
#define MIDI_STATUS_NOTE_ON         0x90
#define MIDI_STATUS_POLY_PRESSURE   0xA0
#define MIDI_STATUS_CONTROL_CHANGE  0xB0
#define MIDI_STATUS_PROGRAM_CHANGE  0xC0
#define MIDI_STATUS_CHANNEL_PRESSURE 0xD0
#define MIDI_STATUS_PITCH_BEND      0xE0

/** MIDI system real-time messages */
#define MIDI_RT_CLOCK               0xF8
#define MIDI_RT_START               0xFA
#define MIDI_RT_CONTINUE            0xFB
#define MIDI_RT_STOP                0xFC
#define MIDI_RT_ACTIVE_SENSE        0xFE
#define MIDI_RT_RESET               0xFF

/** MIDI system common messages */
#define MIDI_SYS_SYSEX_START        0xF0
#define MIDI_SYS_SYSEX_END          0xF7
#define MIDI_SYS_SONG_POSITION      0xF2
#define MIDI_SYS_SONG_SELECT        0xF3

/** MIDI timing */
#define MIDI_CLOCKS_PER_BEAT        24

/** Default CC mappings (DJ-style) */
#define MIDI_CC_VOLUME              0x07  ///< Volume (CC 7)
#define MIDI_CC_PAN                 0x0A  ///< Pan (CC 10)
#define MIDI_CC_EXPRESSION          0x0B  ///< Expression (CC 11)
#define MIDI_CC_FILTER              0x4A  ///< Filter cutoff (CC 74)
#define MIDI_CC_RESONANCE           0x47  ///< Filter resonance (CC 71)
#define MIDI_CC_EQ_LOW              0x50  ///< EQ Low (CC 80) - Custom
#define MIDI_CC_EQ_MID              0x51  ///< EQ Mid (CC 81) - Custom
#define MIDI_CC_EQ_HIGH             0x52  ///< EQ High (CC 82) - Custom
#define MIDI_CC_TEMPO               0x53  ///< Tempo adjust (CC 83) - Custom
#define MIDI_CC_CROSSFADER          0x54  ///< Crossfader (CC 84) - Custom
#define MIDI_CC_EFFECT_WET          0x5B  ///< Effect wet/dry (CC 91)

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief MIDI input handle (opaque)
 */
typedef struct midi_in_s midi_in_t;

/**
 * @brief Deck control target for CC mapping
 */
typedef enum {
    MIDI_DECK_A = 0,
    MIDI_DECK_B = 1,
    MIDI_DECK_MASTER = 2,
    MIDI_DECK_COUNT
} midi_deck_t;

/**
 * @brief Control type for CC mapping
 */
typedef enum {
    MIDI_CTRL_NONE = 0,
    MIDI_CTRL_VOLUME,           ///< Deck volume (0.0-1.0)
    MIDI_CTRL_TEMPO,            ///< Tempo adjust (relative)
    MIDI_CTRL_FILTER_CUTOFF,    ///< Filter cutoff frequency
    MIDI_CTRL_FILTER_RESONANCE, ///< Filter resonance
    MIDI_CTRL_EQ_LOW,           ///< Low EQ (-1.0 to +1.0)
    MIDI_CTRL_EQ_MID,           ///< Mid EQ (-1.0 to +1.0)
    MIDI_CTRL_EQ_HIGH,          ///< High EQ (-1.0 to +1.0)
    MIDI_CTRL_CROSSFADER,       ///< Crossfader position
    MIDI_CTRL_EFFECT_WET,       ///< Effect wet/dry mix
    MIDI_CTRL_JOG_PITCH,        ///< Jog wheel pitch bend
    MIDI_CTRL_COUNT
} midi_ctrl_type_t;

/**
 * @brief Transport event types
 */
typedef enum {
    MIDI_TRANSPORT_PLAY,
    MIDI_TRANSPORT_STOP,
    MIDI_TRANSPORT_CONTINUE,
    MIDI_TRANSPORT_CUE,
    MIDI_TRANSPORT_SYNC
} midi_transport_event_t;

/**
 * @brief CC mapping entry
 */
typedef struct {
    uint8_t cc_number;          ///< CC number (0-127)
    uint8_t channel;            ///< MIDI channel (0-15, 0xFF = any)
    midi_deck_t deck;           ///< Target deck
    midi_ctrl_type_t ctrl;      ///< Control type
    float min_value;            ///< Minimum mapped value
    float max_value;            ///< Maximum mapped value
    bool invert;                ///< Invert value range
} midi_cc_mapping_t;

/**
 * @brief Callback for control value changes
 * 
 * @param deck Target deck
 * @param ctrl Control type
 * @param value Normalized value (typically 0.0-1.0 or -1.0 to 1.0)
 * @param arg User argument
 */
typedef void (*midi_control_cb_t)(midi_deck_t deck, midi_ctrl_type_t ctrl, 
                                   float value, void *arg);

/**
 * @brief Callback for transport events
 * 
 * @param event Transport event type
 * @param deck Target deck (or MASTER for global)
 * @param arg User argument
 */
typedef void (*midi_transport_cb_t)(midi_transport_event_t event, 
                                     midi_deck_t deck, void *arg);

/**
 * @brief Callback for MIDI clock events
 * 
 * @param bpm Detected BPM (updated on each beat)
 * @param phase Phase within current beat (0.0-1.0)
 * @param beat_count Total beats since start
 * @param arg User argument
 */
typedef void (*midi_clock_cb_t)(float bpm, float phase, 
                                 uint32_t beat_count, void *arg);

/**
 * @brief Callback for note events
 * 
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Velocity (0-127, 0 = note off)
 * @param arg User argument
 */
typedef void (*midi_note_cb_t)(uint8_t channel, uint8_t note, 
                                uint8_t velocity, void *arg);

/**
 * @brief MIDI input configuration
 */
typedef struct {
    int uart_num;               ///< UART number (0-2)
    int rx_pin;                 ///< RX GPIO pin
    int tx_pin;                 ///< TX GPIO pin (-1 to disable output)
    uint8_t listen_channel;     ///< MIDI channel filter (0-15, 0xFF = omni)
    bool enable_clock_sync;     ///< Enable MIDI clock reception
    bool enable_thru;           ///< Enable MIDI thru (echo input to output)
    
    // Callbacks
    midi_control_cb_t control_cb;
    midi_transport_cb_t transport_cb;
    midi_clock_cb_t clock_cb;
    midi_note_cb_t note_cb;
    void *callback_arg;
} midi_in_config_t;

/* ============================================================================
 * Functions
 * ============================================================================ */

/**
 * @brief Get default configuration
 * 
 * @return Default config with typical DJ mapping
 */
midi_in_config_t midi_in_get_default_config(void);

/**
 * @brief Create MIDI input instance
 * 
 * @param config Configuration
 * @return MIDI input handle or NULL on error
 */
midi_in_t *midi_in_create(const midi_in_config_t *config);

/**
 * @brief Destroy MIDI input instance
 * 
 * @param midi MIDI input handle
 */
void midi_in_destroy(midi_in_t *midi);

/**
 * @brief Start MIDI input processing
 * 
 * Starts the UART receive task.
 * 
 * @param midi MIDI input handle
 * @return true on success
 */
bool midi_in_start(midi_in_t *midi);

/**
 * @brief Stop MIDI input processing
 * 
 * @param midi MIDI input handle
 */
void midi_in_stop(midi_in_t *midi);

/* ============================================================================
 * CC Mapping
 * ============================================================================ */

/**
 * @brief Add CC mapping
 * 
 * Maps a MIDI CC number to a deck control.
 * 
 * @param midi MIDI input handle
 * @param mapping Mapping configuration
 * @return true on success, false if mapping table full
 */
bool midi_in_add_cc_mapping(midi_in_t *midi, const midi_cc_mapping_t *mapping);

/**
 * @brief Remove CC mapping
 * 
 * @param midi MIDI input handle
 * @param cc_number CC number to unmap
 * @param channel Channel (0xFF = all channels)
 */
void midi_in_remove_cc_mapping(midi_in_t *midi, uint8_t cc_number, uint8_t channel);

/**
 * @brief Clear all CC mappings
 * 
 * @param midi MIDI input handle
 */
void midi_in_clear_mappings(midi_in_t *midi);

/**
 * @brief Load default DJ CC mappings
 * 
 * Loads a standard DJ-style CC mapping preset.
 * 
 * @param midi MIDI input handle
 */
void midi_in_load_default_mappings(midi_in_t *midi);

/* ============================================================================
 * Clock Sync
 * ============================================================================ */

/**
 * @brief Get current detected BPM from external clock
 * 
 * @param midi MIDI input handle
 * @return BPM value (0 if no clock received recently)
 */
float midi_in_get_bpm(const midi_in_t *midi);

/**
 * @brief Get clock phase (position within current beat)
 * 
 * @param midi MIDI input handle
 * @return Phase (0.0-1.0)
 */
float midi_in_get_phase(const midi_in_t *midi);

/**
 * @brief Check if external clock is active
 * 
 * @param midi MIDI input handle
 * @return true if receiving clock messages
 */
bool midi_in_is_clock_active(const midi_in_t *midi);

/**
 * @brief Check if external transport is running
 * 
 * @param midi MIDI input handle
 * @return true if received START and not STOP
 */
bool midi_in_is_transport_running(const midi_in_t *midi);

/**
 * @brief Get beat count since transport start
 * 
 * @param midi MIDI input handle
 * @return Beat count
 */
uint32_t midi_in_get_beat_count(const midi_in_t *midi);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Get MIDI message statistics
 * 
 * @param midi MIDI input handle
 * @param clock_count Output: clock messages received
 * @param cc_count Output: CC messages received
 * @param note_count Output: note messages received
 * @param error_count Output: parse errors
 */
void midi_in_get_stats(const midi_in_t *midi, uint32_t *clock_count,
                       uint32_t *cc_count, uint32_t *note_count,
                       uint32_t *error_count);

/**
 * @brief Reset statistics
 * 
 * @param midi MIDI input handle
 */
void midi_in_reset_stats(midi_in_t *midi);

#ifdef __cplusplus
}
#endif

#endif // MIDI_IN_H
