/**
 * @file audio_player.h
 * @brief Audio player interface for MP3 playback
 */

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Player mode
 */
typedef enum {
    AUDIO_PLAYER_MODE_SIMPLE = 0,    ///< Simple direct playback mode
    AUDIO_PLAYER_MODE_GRANULAR       ///< Granular synthesis mode with streaming buffer
} audio_player_mode_t;

/**
 * @brief Player state
 */
typedef enum {
    AUDIO_PLAYER_STATE_STOPPED,
    AUDIO_PLAYER_STATE_PLAYING,
    AUDIO_PLAYER_STATE_PAUSED
} audio_player_state_t;

/**
 * @brief Initialize audio player
 * 
 * @return true on success, false on failure
 */
bool audio_player_init(void);

/**
 * @brief Deinitialize audio player
 */
void audio_player_deinit(void);

/**
 * @brief Load and prepare a track for playback
 * 
 * @param filepath Path to MP3 file
 * @return true on success, false on failure
 */
bool audio_player_load(const char *filepath);

/**
 * @brief Start playback
 * 
 * @return true on success, false on failure
 */
bool audio_player_play(void);

/**
 * @brief Pause playback
 */
void audio_player_pause(void);

/**
 * @brief Resume playback
 */
void audio_player_resume(void);

/**
 * @brief Stop playback
 */
void audio_player_stop(void);

/**
 * @brief Get current player state
 * 
 * @return Player state
 */
audio_player_state_t audio_player_get_state(void);

/**
 * @brief Get current playback position in seconds
 * 
 * @return Position in seconds
 */
uint32_t audio_player_get_position(void);

/**
 * @brief Get track duration in seconds
 * 
 * @return Duration in seconds
 */
uint32_t audio_player_get_duration(void);

/**
 * @brief Set playback position
 * 
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool audio_player_seek(uint32_t position);

/**
 * @brief Update player (call in main loop)
 */
void audio_player_update(void);

/**
 * @brief Set player mode (simple or granular)
 * 
 * @param mode Mode to use
 * @return true on success, false on failure
 */
bool audio_player_set_mode(audio_player_mode_t mode);

/**
 * @brief Get current player mode
 * 
 * @return Current mode
 */
audio_player_mode_t audio_player_get_mode(void);

/**
 * @brief Set granular engine speed (for granular mode)
 * 
 * @param speed Speed (0.0 = freeze, 1.0 = normal, >1.0 = faster)
 */
void audio_player_set_granular_speed(float speed);

/**
 * @brief Set granular engine grain size (for granular mode)
 * 
 * @param grain_size_ms Grain size in milliseconds
 */
void audio_player_set_granular_grain_size(float grain_size_ms);

/**
 * @brief Set granular engine pitch (for granular mode)
 * 
 * @param pitch Pitch multiplier (1.0 = normal)
 */
void audio_player_set_granular_pitch(float pitch);

/**
 * @brief Set granular engine jitter (for granular mode)
 * 
 * @param jitter Jitter amount (0.0 = none, 1.0 = maximum)
 */
void audio_player_set_granular_jitter(float jitter);

/**
 * @brief Set output gain (volume)
 * 
 * @param gain Gain multiplier (0.0 to 1.0)
 */
void audio_player_set_gain(float gain);

/**
 * @brief Get the title of the current track (or filename if no tag)
 * 
 * @return const char* Track title or empty string
 */
const char* audio_player_get_track_title(void);

/**
 * @brief Get current waveform data for UI
 * 
 * @param buffer Output buffer (should be 480 bytes)
 * @param size Buffer size
 */
void audio_player_get_waveform(uint8_t *buffer, size_t size);

/**
 * @brief Get current playback position in seconds (precise)
 * 
 * @return Position in seconds as float
 */
float audio_player_get_precise_position(void);

/**
 * @brief Get current waveform buffer index (sample-accurate position)
 * 
 * Used for ring buffer scroll optimization in waveform display.
 * 
 * @return Current waveform buffer index
 */
size_t audio_player_get_waveform_index(void);

// ============================================================================
// Metadata API (OpenDeck .odk integration)
// ============================================================================

/**
 * @brief Get pre-analyzed overview waveform for navigation stripe
 * 
 * Returns 480 bytes of normalized amplitude data from .odk file.
 * This is different from get_waveform() which returns real-time data.
 * 
 * @param buffer Output buffer (must be at least size bytes)
 * @param size   Requested size (max WAVEFORM_POINTS=480)
 * @return true if overview available, false if not analyzed yet
 */
bool audio_player_get_overview(uint8_t *buffer, size_t size);

/**
 * @brief Seek to percentage position using VBR-aware seek table
 * 
 * Uses pre-analyzed seek table from .odk file for accurate VBR seeking.
 * 
 * @param percent Position as percentage (0.0 to 1.0)
 * @return true on success, false if no metadata available
 */
bool audio_player_seek_percent(float percent);

/**
 * @brief Get detected BPM from metadata
 * 
 * @return BPM value (e.g., 128.0) or 0.0 if not analyzed
 */
float audio_player_get_bpm(void);

/**
 * @brief Get detected musical key from metadata
 * 
 * @return Camelot key ID (0-23) or -1 if not analyzed
 */
int audio_player_get_key(void);

/**
 * @brief Get Camelot key name string
 * 
 * @return Key name string (e.g., "8A", "5B") or "?" if unknown
 */
const char* audio_player_get_key_name(void);

/**
 * @brief Check if current track has complete metadata analysis
 * 
 * @return true if waveform and BPM are available
 */
bool audio_player_has_metadata(void);

/**
 * @brief Get position in milliseconds
 * 
 * @return Current playback position in milliseconds
 */
uint32_t audio_player_get_position_ms(void);

/**
 * @brief Get duration in milliseconds
 * 
 * Uses metadata if available, otherwise calculates from bitrate.
 * 
 * @return Track duration in milliseconds
 */
uint32_t audio_player_get_duration_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */

