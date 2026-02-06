/**
 * @file analog_sync.h
 * @brief DIN sync implementation at 24ppqn
 * 
 * Implements full DIN sync protocol compatible with:
 * - Roland TR-x0x series
 * - Korg Volcas
 * - Teenage Engineering Pocket Operators
 * - Other DIN sync / sync24 compatible gear
 * 
 * Features:
 * - Master mode: Generate 24ppqn clock with start/stop/continue
 * - Slave mode: Follow external DIN sync with clock recovery
 * - Jitter compensation and filtering
 * - LED status callbacks for UI integration
 */

#ifndef ANALOG_SYNC_H
#define ANALOG_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** DIN sync standard: 24 pulses per quarter note */
#define ANALOG_SYNC_PPQN 24

/**
 * @brief Analog sync handle
 */
typedef struct analog_sync_s analog_sync_t;

/**
 * @brief Sync mode
 */
typedef enum {
    ANALOG_SYNC_MODE_OFF = 0,    ///< Sync disabled
    ANALOG_SYNC_MODE_MASTER,     ///< Generate DIN sync clock
    ANALOG_SYNC_MODE_SLAVE       ///< Follow external DIN sync
} analog_sync_mode_t;

/**
 * @brief Transport events
 */
typedef enum {
    ANALOG_SYNC_EVENT_START = 0, ///< Transport started
    ANALOG_SYNC_EVENT_STOP,      ///< Transport stopped
    ANALOG_SYNC_EVENT_CONTINUE,  ///< Transport continued
    ANALOG_SYNC_EVENT_BEAT,      ///< Beat boundary (every 24 pulses)
    ANALOG_SYNC_EVENT_CLOCK_LOST ///< External clock signal lost
} analog_sync_event_t;

/**
 * @brief LED indicator states
 */
typedef enum {
    ANALOG_SYNC_LED_OFF = 0,     ///< Sync disabled
    ANALOG_SYNC_LED_WAITING,     ///< Slave waiting for clock
    ANALOG_SYNC_LED_LOCKED,      ///< Slave locked to clock
    ANALOG_SYNC_LED_RUNNING,     ///< Transport running
    ANALOG_SYNC_LED_STOPPED      ///< Transport stopped (master ready)
} analog_sync_led_state_t;

/**
 * @brief Pin configuration for DIN sync
 */
typedef struct {
    int clock_out_pin;  ///< GPIO for clock output (-1 to disable)
    int clock_in_pin;   ///< GPIO for clock input (-1 to disable)
    int run_out_pin;    ///< GPIO for run/stop output (-1 to disable)
    int run_in_pin;     ///< GPIO for run/stop input (-1 to disable)
} analog_sync_config_t;

/**
 * @brief Transport event callback
 * 
 * @param sync      Sync handle
 * @param event     Event type
 * @param user_data User data passed to callback
 */
typedef void (*analog_sync_callback_t)(analog_sync_t *sync, 
                                        analog_sync_event_t event,
                                        void *user_data);

/**
 * @brief LED state change callback
 * 
 * @param sync      Sync handle
 * @param state     New LED state
 * @param user_data User data passed to callback
 */
typedef void (*analog_sync_led_callback_t)(analog_sync_t *sync,
                                            analog_sync_led_state_t state,
                                            void *user_data);

// ============================================================================
// Creation and destruction
// ============================================================================

/**
 * @brief Create analog sync instance with full configuration
 * 
 * @param config Pin configuration
 * @return Analog sync handle or NULL on error
 */
analog_sync_t *analog_sync_create(const analog_sync_config_t *config);

/**
 * @brief Create analog sync instance (simple - output only)
 * 
 * Backward compatible with original API. Creates output-only sync
 * on specified GPIO pin.
 * 
 * @param gpio_pin GPIO pin for sync pulse output
 * @return Analog sync handle or NULL on error
 */
analog_sync_t *analog_sync_create_simple(int gpio_pin);

/**
 * @brief Destroy analog sync instance
 * 
 * @param sync Analog sync handle
 */
void analog_sync_destroy(analog_sync_t *sync);

// ============================================================================
// Mode and configuration
// ============================================================================

/**
 * @brief Set sync mode
 * 
 * @param sync Analog sync handle
 * @param mode Sync mode (master/slave/off)
 */
void analog_sync_set_mode(analog_sync_t *sync, analog_sync_mode_t mode);

/**
 * @brief Get current sync mode
 * 
 * @param sync Analog sync handle
 * @return Current mode
 */
analog_sync_mode_t analog_sync_get_mode(const analog_sync_t *sync);

/**
 * @brief Set master BPM
 * 
 * In master mode, this sets the clock rate.
 * In slave mode, this is ignored (BPM comes from external clock).
 * 
 * @param sync Analog sync handle
 * @param bpm BPM value (30-300)
 */
void analog_sync_set_bpm(analog_sync_t *sync, float bpm);

/**
 * @brief Get current BPM
 * 
 * Returns slave-detected BPM in slave mode, master setting otherwise.
 * 
 * @param sync Analog sync handle
 * @return Current BPM
 */
float analog_sync_get_bpm(const analog_sync_t *sync);

/**
 * @brief Set swing amount
 * 
 * Delays every second 16th note pulse to create swing feel.
 * Only applies in master mode.
 * 
 * @param sync Analog sync handle
 * @param swing_ms Swing delay in milliseconds (0-50)
 */
void analog_sync_set_swing(analog_sync_t *sync, float swing_ms);

// ============================================================================
// Transport control
// ============================================================================

/**
 * @brief Start sync (master mode)
 * 
 * In master mode: Starts clock output and sets RUN signal high.
 * In slave mode: No effect (follows external transport).
 * 
 * @param sync Analog sync handle
 */
void analog_sync_start(analog_sync_t *sync);

/**
 * @brief Stop sync (master mode)
 * 
 * In master mode: Stops clock output and sets RUN signal low.
 * In slave mode: No effect.
 * 
 * @param sync Analog sync handle
 */
void analog_sync_stop(analog_sync_t *sync);

/**
 * @brief Continue sync (master mode)
 * 
 * Like start, but doesn't reset pulse counter/phase.
 * 
 * @param sync Analog sync handle
 */
void analog_sync_continue(analog_sync_t *sync);

/**
 * @brief Check if transport is running
 * 
 * @param sync Analog sync handle
 * @return True if running
 */
bool analog_sync_is_running(const analog_sync_t *sync);

/**
 * @brief Check if slave is locked to external clock
 * 
 * Only meaningful in slave mode.
 * 
 * @param sync Analog sync handle
 * @return True if locked to external clock
 */
bool analog_sync_is_locked(const analog_sync_t *sync);

/**
 * @brief Get current beat phase
 * 
 * Returns 0.0 to 1.0 representing position within current beat.
 * 0.0 = beat start, 0.5 = halfway, etc.
 * 
 * @param sync Analog sync handle
 * @return Phase (0.0 to 1.0)
 */
float analog_sync_get_phase(const analog_sync_t *sync);

/**
 * @brief Get total pulse count since start
 * 
 * @param sync Analog sync handle
 * @return Pulse count
 */
uint32_t analog_sync_get_pulse_count(const analog_sync_t *sync);

/**
 * @brief Reset phase to zero
 * 
 * @param sync Analog sync handle
 */
void analog_sync_reset_phase(analog_sync_t *sync);

// ============================================================================
// Callbacks
// ============================================================================

/**
 * @brief Set transport event callback
 * 
 * @param sync      Analog sync handle
 * @param callback  Callback function
 * @param user_data User data passed to callback
 */
void analog_sync_set_callback(analog_sync_t *sync,
                               analog_sync_callback_t callback,
                               void *user_data);

/**
 * @brief Set LED state change callback
 * 
 * @param sync      Analog sync handle
 * @param callback  Callback function
 * @param user_data User data passed to callback
 */
void analog_sync_set_led_callback(analog_sync_t *sync,
                                   analog_sync_led_callback_t callback,
                                   void *user_data);

/**
 * @brief Get current LED indicator state
 * 
 * @param sync Analog sync handle
 * @return LED state
 */
analog_sync_led_state_t analog_sync_get_led_state(const analog_sync_t *sync);

// ============================================================================
// Sync manager integration
// ============================================================================

/**
 * @brief Process tick (call from main loop)
 * 
 * Handles deferred operations from ISR context:
 * - LED state updates
 * - Timeout detection
 * - Beat event notifications
 * 
 * Should be called regularly (e.g., every 10-50ms).
 * 
 * @param sync Analog sync handle
 */
void analog_sync_tick(analog_sync_t *sync);

#ifdef __cplusplus
}
#endif

#endif // ANALOG_SYNC_H
