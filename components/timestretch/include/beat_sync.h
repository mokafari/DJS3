/**
 * @file beat_sync.h
 * @brief Beat synchronization and grid management
 */

#ifndef BEAT_SYNC_H
#define BEAT_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Beat grid information
 */
typedef struct {
    float bpm;                  ///< Beats per minute
    uint32_t first_beat_ms;     ///< First beat offset in milliseconds
    uint8_t signature_numerator;   ///< Time signature numerator (e.g., 4)
    uint8_t signature_denominator; ///< Time signature denominator (e.g., 4)
    bool is_analyzed;          ///< Has this track been analyzed?
} beat_grid_t;

/**
 * @brief Beat sync state
 */
typedef struct {
    float current_bpm;          ///< Current playback BPM
    uint32_t current_position_ms; ///< Current position in milliseconds
    uint32_t next_beat_ms;      ///< Next beat position in milliseconds
    float phase_error;          ///< Phase error (-1.0 to 1.0)
    bool is_synced;             ///< Is currently synced to master?
} beat_sync_state_t;

/**
 * @brief Initialize beat sync
 * 
 * @param grid Beat grid information
 * @return 0 on success
 */
int beat_sync_init(const beat_grid_t *grid);

/**
 * @brief Update beat sync state
 * 
 * @param position_ms Current playback position in milliseconds
 * @param state Output state structure
 */
void beat_sync_update(uint32_t position_ms, beat_sync_state_t *state);

/**
 * @brief Get next beat position
 * 
 * @param position_ms Current position
 * @return Next beat position in milliseconds
 */
uint32_t beat_sync_get_next_beat(uint32_t position_ms);

/**
 * @brief Calculate phase error
 * 
 * @param position_ms Current position
 * @return Phase error (-1.0 = behind, 0.0 = on beat, 1.0 = ahead)
 */
float beat_sync_get_phase_error(uint32_t position_ms);

#ifdef __cplusplus
}
#endif

#endif // BEAT_SYNC_H

