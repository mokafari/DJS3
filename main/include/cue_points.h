/**
 * @file cue_points.h
 * @brief Cue point system interface
 */

#ifndef CUE_POINTS_H
#define CUE_POINTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CUE_POINTS 8
#define MAX_HOT_CUES 4

/**
 * @brief Set a cue point at current position
 * 
 * @param cue_index Cue index (0-7)
 * @param position Position in seconds
 * @return true on success, false on failure
 */
bool cue_points_set(uint8_t cue_index, uint32_t position);

/**
 * @brief Get a cue point position
 * 
 * @param cue_index Cue index (0-7)
 * @return Position in seconds, or 0 if not set
 */
uint32_t cue_points_get(uint8_t cue_index);

/**
 * @brief Clear a cue point
 * 
 * @param cue_index Cue index (0-7)
 */
void cue_points_clear(uint8_t cue_index);

/**
 * @brief Clear all cue points
 */
void cue_points_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* CUE_POINTS_H */

