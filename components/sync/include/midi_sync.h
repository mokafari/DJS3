/**
 * @file midi_sync.h
 * @brief MIDI clock synchronization (master and slave modes)
 */

#ifndef MIDI_SYNC_H
#define MIDI_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MIDI sync handle
 */
typedef struct midi_sync_s midi_sync_t;

/**
 * @brief MIDI sync mode
 */
typedef enum {
    MIDI_SYNC_MODE_OFF = 0,
    MIDI_SYNC_MODE_MASTER,  ///< Generate MIDI clock
    MIDI_SYNC_MODE_SLAVE    ///< Follow external MIDI clock
} midi_sync_mode_t;

/**
 * @brief Create MIDI sync instance
 * 
 * @param uart_num UART number for MIDI output
 * @param tx_pin TX pin for MIDI output
 * @return MIDI sync handle or NULL on error
 */
midi_sync_t *midi_sync_create(int uart_num, int tx_pin);

/**
 * @brief Destroy MIDI sync instance
 * 
 * @param sync MIDI sync handle
 */
void midi_sync_destroy(midi_sync_t *sync);

/**
 * @brief Set sync mode
 * 
 * @param sync MIDI sync handle
 * @param mode Sync mode (master/slave/off)
 */
void midi_sync_set_mode(midi_sync_t *sync, midi_sync_mode_t mode);

/**
 * @brief Set master BPM (for master mode)
 * 
 * @param sync MIDI sync handle
 * @param bpm BPM value (60-180)
 */
void midi_sync_set_bpm(midi_sync_t *sync, float bpm);

/**
 * @brief Get current BPM (from slave clock or master setting)
 * 
 * @param sync MIDI sync handle
 * @return Current BPM
 */
float midi_sync_get_bpm(const midi_sync_t *sync);

/**
 * @brief Start MIDI clock (master mode)
 * 
 * @param sync MIDI sync handle
 */
void midi_sync_start(midi_sync_t *sync);

/**
 * @brief Stop MIDI clock (master mode)
 * 
 * @param sync MIDI sync handle
 */
void midi_sync_stop(midi_sync_t *sync);

/**
 * @brief Process incoming MIDI data (slave mode)
 * 
 * @param sync MIDI sync handle
 * @param data MIDI byte
 */
void midi_sync_process_byte(midi_sync_t *sync, uint8_t data);

/**
 * @brief Check if clock is running
 * 
 * @param sync MIDI sync handle
 * @return True if clock is running
 */
bool midi_sync_is_running(const midi_sync_t *sync);

/**
 * @brief Get clock phase (0.0 to 1.0 within current beat)
 * 
 * @param sync MIDI sync handle
 * @return Phase value (0.0 to 1.0)
 */
float midi_sync_get_phase(const midi_sync_t *sync);

#ifdef __cplusplus
}
#endif

#endif // MIDI_SYNC_H

