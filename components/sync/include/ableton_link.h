/**
 * @file ableton_link.h
 * @brief Ableton Link protocol implementation for ESP32
 * 
 * Implements the Ableton Link protocol for synchronizing tempo, beat, and phase
 * across networked devices. Uses ESP32 Wi-Fi for peer discovery and communication.
 * 
 * Reference: https://github.com/Ableton/link
 * Protocol: UDP multicast on 224.76.78.75:20808
 */

#ifndef ABLETON_LINK_H
#define ABLETON_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ableton Link handle
 */
typedef struct ableton_link_s ableton_link_t;

/**
 * @brief Link session state (snapshot for thread-safe access)
 */
typedef struct {
    double tempo;              ///< Current tempo in BPM
    double beat;               ///< Current beat position
    double phase;              ///< Phase within quantum (0.0 to quantum)
    int64_t time_us;           ///< Timestamp in microseconds
    bool is_playing;           ///< Transport playing state
    int64_t start_stop_time;   ///< Time of last start/stop change
} link_session_state_t;

/**
 * @brief Link configuration
 */
typedef struct {
    double initial_tempo;      ///< Initial tempo in BPM (default: 120.0)
    double quantum;            ///< Phase sync quantum in beats (default: 4.0)
    bool start_stop_sync;      ///< Enable start/stop synchronization
} link_config_t;

/**
 * @brief Callback for peer count changes
 */
typedef void (*link_peer_callback_t)(void *user_data, size_t num_peers);

/**
 * @brief Callback for tempo changes
 */
typedef void (*link_tempo_callback_t)(void *user_data, double bpm);

/**
 * @brief Callback for start/stop state changes
 */
typedef void (*link_start_stop_callback_t)(void *user_data, bool is_playing);

/**
 * @brief Default configuration initializer
 */
#define LINK_CONFIG_DEFAULT() { \
    .initial_tempo = 120.0, \
    .quantum = 4.0, \
    .start_stop_sync = false \
}

/**
 * @brief Create Ableton Link instance
 * 
 * @param config Configuration parameters
 * @return Link handle or NULL on error
 */
ableton_link_t *ableton_link_create(const link_config_t *config);

/**
 * @brief Destroy Ableton Link instance
 * 
 * @param link Link handle
 */
void ableton_link_destroy(ableton_link_t *link);

/**
 * @brief Enable or disable Link
 * 
 * When enabled, Link will start network discovery and synchronization.
 * 
 * @param link Link handle
 * @param enable True to enable, false to disable
 * @return ESP_OK on success
 */
int ableton_link_enable(ableton_link_t *link, bool enable);

/**
 * @brief Check if Link is enabled
 * 
 * @param link Link handle
 * @return True if enabled
 */
bool ableton_link_is_enabled(const ableton_link_t *link);

/**
 * @brief Get number of connected peers
 * 
 * @param link Link handle
 * @return Number of peers (0 if alone in session)
 */
size_t ableton_link_num_peers(const ableton_link_t *link);

/**
 * @brief Enable or disable start/stop synchronization
 * 
 * @param link Link handle
 * @param enable True to enable
 */
void ableton_link_enable_start_stop_sync(ableton_link_t *link, bool enable);

/**
 * @brief Check if start/stop sync is enabled
 * 
 * @param link Link handle
 * @return True if enabled
 */
bool ableton_link_is_start_stop_sync_enabled(const ableton_link_t *link);

/**
 * @brief Set callback for peer count changes
 * 
 * @param link Link handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
void ableton_link_set_peer_callback(ableton_link_t *link, 
                                    link_peer_callback_t callback,
                                    void *user_data);

/**
 * @brief Set callback for tempo changes
 * 
 * @param link Link handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
void ableton_link_set_tempo_callback(ableton_link_t *link,
                                     link_tempo_callback_t callback,
                                     void *user_data);

/**
 * @brief Set callback for start/stop state changes
 * 
 * @param link Link handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
void ableton_link_set_start_stop_callback(ableton_link_t *link,
                                          link_start_stop_callback_t callback,
                                          void *user_data);

/**
 * @brief Capture current session state (audio thread safe)
 * 
 * Use this in the audio callback for timing-critical operations.
 * The returned state is a snapshot - safe to use in local scope.
 * 
 * @param link Link handle
 * @param state Output session state
 */
void ableton_link_capture_audio_state(const ableton_link_t *link, 
                                      link_session_state_t *state);

/**
 * @brief Capture current session state (application thread)
 * 
 * Use this for UI updates and non-realtime operations.
 * 
 * @param link Link handle
 * @param state Output session state
 */
void ableton_link_capture_app_state(const ableton_link_t *link,
                                    link_session_state_t *state);

/**
 * @brief Get tempo from session state
 * 
 * @param state Session state
 * @return Tempo in BPM
 */
double ableton_link_state_tempo(const link_session_state_t *state);

/**
 * @brief Set tempo in session state
 * 
 * @param state Session state
 * @param bpm New tempo
 * @param at_time Time at which change takes effect (microseconds)
 */
void ableton_link_state_set_tempo(link_session_state_t *state, 
                                  double bpm, 
                                  int64_t at_time);

/**
 * @brief Get beat at given time
 * 
 * @param state Session state
 * @param time_us Time in microseconds
 * @param quantum Phase synchronization quantum
 * @return Beat value
 */
double ableton_link_state_beat_at_time(const link_session_state_t *state,
                                       int64_t time_us,
                                       double quantum);

/**
 * @brief Get phase at given time
 * 
 * Returns value in range [0, quantum)
 * 
 * @param state Session state
 * @param time_us Time in microseconds
 * @param quantum Phase synchronization quantum
 * @return Phase value
 */
double ableton_link_state_phase_at_time(const link_session_state_t *state,
                                        int64_t time_us,
                                        double quantum);

/**
 * @brief Get time at given beat
 * 
 * @param state Session state
 * @param beat Beat value
 * @param quantum Phase synchronization quantum
 * @return Time in microseconds
 */
int64_t ableton_link_state_time_at_beat(const link_session_state_t *state,
                                        double beat,
                                        double quantum);

/**
 * @brief Request beat at time (quantized)
 * 
 * Maps the given beat to the given time, respecting the quantum.
 * If peers are connected, waits for next quantum boundary.
 * 
 * @param state Session state
 * @param beat Desired beat value
 * @param time_us Time in microseconds
 * @param quantum Phase synchronization quantum
 */
void ableton_link_state_request_beat_at_time(link_session_state_t *state,
                                             double beat,
                                             int64_t time_us,
                                             double quantum);

/**
 * @brief Force beat at time (immediate, use with caution)
 * 
 * Unconditionally maps beat to time. This can disrupt other peers.
 * Only use for external clock synchronization.
 * 
 * @param state Session state
 * @param beat Beat value
 * @param time_us Time in microseconds
 * @param quantum Phase synchronization quantum
 */
void ableton_link_state_force_beat_at_time(link_session_state_t *state,
                                           double beat,
                                           int64_t time_us,
                                           double quantum);

/**
 * @brief Check if transport is playing
 * 
 * @param state Session state
 * @return True if playing
 */
bool ableton_link_state_is_playing(const link_session_state_t *state);

/**
 * @brief Set playing state
 * 
 * @param state Session state
 * @param is_playing Playing state
 * @param time_us Time at which change takes effect
 */
void ableton_link_state_set_is_playing(link_session_state_t *state,
                                       bool is_playing,
                                       int64_t time_us);

/**
 * @brief Commit session state from audio thread
 * 
 * @param link Link handle
 * @param state Modified session state
 */
void ableton_link_commit_audio_state(ableton_link_t *link,
                                     const link_session_state_t *state);

/**
 * @brief Commit session state from application thread
 * 
 * @param link Link handle
 * @param state Modified session state
 */
void ableton_link_commit_app_state(ableton_link_t *link,
                                   const link_session_state_t *state);

/**
 * @brief Get current time in microseconds
 * 
 * Returns the Link clock time, suitable for use with session state methods.
 * 
 * @param link Link handle
 * @return Current time in microseconds
 */
int64_t ableton_link_clock_micros(const ableton_link_t *link);

/**
 * @brief Set quantum for phase synchronization
 * 
 * @param link Link handle
 * @param quantum Quantum in beats (typically 4.0 for 4/4 time)
 */
void ableton_link_set_quantum(ableton_link_t *link, double quantum);

/**
 * @brief Get current quantum
 * 
 * @param link Link handle
 * @return Quantum in beats
 */
double ableton_link_get_quantum(const ableton_link_t *link);

#ifdef __cplusplus
}
#endif

#endif // ABLETON_LINK_H
