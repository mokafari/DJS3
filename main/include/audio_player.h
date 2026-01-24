/**
 * @file audio_player.h
 * @brief Audio player interface for MP3 playback
 */

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */

