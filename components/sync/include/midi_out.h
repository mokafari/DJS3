/**
 * @file midi_out.h
 * @brief MIDI output module for DJ deck synchronization
 * 
 * Provides MIDI clock output at 24 PPQN synchronized to deck playback,
 * transport messages (start/stop/continue), and CC messages for BPM.
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MIDI output handle
 */
typedef struct midi_out_s midi_out_t;

/**
 * @brief MIDI output configuration
 */
typedef struct {
    int uart_num;           ///< UART number for MIDI output
    int tx_pin;             ///< TX pin for MIDI output
    uint8_t bpm_cc_msb;     ///< CC number for BPM MSB (default: 51)
    uint8_t bpm_cc_lsb;     ///< CC number for BPM LSB (default: 52)
    uint8_t channel;        ///< MIDI channel (0-15, default: 0)
    bool send_bpm_cc;       ///< Enable BPM CC transmission
} midi_out_config_t;

/**
 * @brief Default configuration initializer
 */
#define MIDI_OUT_CONFIG_DEFAULT() { \
    .uart_num = 1,              \
    .tx_pin = -1,               \
    .bpm_cc_msb = 51,           \
    .bpm_cc_lsb = 52,           \
    .channel = 0,               \
    .send_bpm_cc = true         \
}

/**
 * @brief Deck state for MIDI synchronization
 */
typedef struct {
    bool is_playing;            ///< Deck is currently playing
    float bpm;                  ///< Current BPM (including pitch adjustment)
    uint32_t position_ms;       ///< Current playback position in milliseconds
    float phase;                ///< Beat phase (0.0 to 1.0)
    uint32_t first_beat_ms;     ///< First beat offset from track start
} midi_out_deck_state_t;

/**
 * @brief Create MIDI output instance
 * 
 * @param config Configuration structure
 * @return MIDI output handle or NULL on error
 */
midi_out_t *midi_out_create(const midi_out_config_t *config);

/**
 * @brief Destroy MIDI output instance
 * 
 * @param out MIDI output handle
 */
void midi_out_destroy(midi_out_t *out);

/**
 * @brief Start MIDI clock output
 * 
 * Sends MIDI Start message and begins clock generation.
 * 
 * @param out MIDI output handle
 * @param bpm Initial BPM
 */
void midi_out_start(midi_out_t *out, float bpm);

/**
 * @brief Stop MIDI clock output
 * 
 * Sends MIDI Stop message and halts clock.
 * 
 * @param out MIDI output handle
 */
void midi_out_stop(midi_out_t *out);

/**
 * @brief Continue MIDI clock output
 * 
 * Sends MIDI Continue message and resumes clock from current position.
 * 
 * @param out MIDI output handle
 */
void midi_out_continue(midi_out_t *out);

/**
 * @brief Update BPM (changes clock rate)
 * 
 * Adjusts the MIDI clock interval based on new BPM.
 * Also sends BPM as CC messages if enabled.
 * 
 * @param out MIDI output handle
 * @param bpm New BPM value
 */
void midi_out_set_bpm(midi_out_t *out, float bpm);

/**
 * @brief Get current BPM
 * 
 * @param out MIDI output handle
 * @return Current BPM
 */
float midi_out_get_bpm(const midi_out_t *out);

/**
 * @brief Send BPM as CC messages
 * 
 * Transmits BPM value using two CC messages (MSB/LSB) for ~0.1 BPM precision.
 * BPM is encoded as: value = (bpm - 60) * 100 (range: 60-187 BPM)
 * MSB = value >> 7, LSB = value & 0x7F
 * 
 * @param out MIDI output handle
 * @param bpm BPM value to transmit
 */
void midi_out_send_bpm_cc(midi_out_t *out, float bpm);

/**
 * @brief Synchronize with deck playback
 * 
 * Call this regularly (e.g., every audio callback) to maintain sync.
 * Adjusts clock timing based on deck state.
 * 
 * @param out MIDI output handle
 * @param state Current deck state
 */
void midi_out_sync_to_deck(midi_out_t *out, const midi_out_deck_state_t *state);

/**
 * @brief Check if MIDI clock is running
 * 
 * @param out MIDI output handle
 * @return True if clock is running
 */
bool midi_out_is_running(const midi_out_t *out);

/**
 * @brief Get current clock pulse count
 * 
 * Returns the number of MIDI clock pulses sent since start.
 * Useful for debugging and position tracking.
 * 
 * @param out MIDI output handle
 * @return Pulse count (24 pulses = 1 beat)
 */
uint32_t midi_out_get_pulse_count(const midi_out_t *out);

/**
 * @brief Send arbitrary CC message
 * 
 * @param out MIDI output handle
 * @param cc CC number (0-127)
 * @param value CC value (0-127)
 */
void midi_out_send_cc(midi_out_t *out, uint8_t cc, uint8_t value);

/**
 * @brief Send Song Position Pointer (SPP)
 * 
 * Used to set position in external sequencers (in MIDI beats = 6 clocks).
 * 
 * @param out MIDI output handle
 * @param position Position in MIDI beats (1 beat = 6 clocks)
 */
void midi_out_send_spp(midi_out_t *out, uint16_t position);

#ifdef __cplusplus
}
#endif

#endif // MIDI_OUT_H
