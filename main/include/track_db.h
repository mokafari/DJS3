/**
 * @file track_db.h
 * @brief Track database with ID3 tag parsing and track browser
 */

#ifndef TRACK_DB_H
#define TRACK_DB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TRACKS 200  // Reduced from 1000 to save DRAM
#define MAX_FILENAME_LEN 256
#define MAX_TITLE_LEN 128
#define MAX_ARTIST_LEN 128

/**
 * @brief Track information structure
 */
typedef struct {
    char filename[MAX_FILENAME_LEN];
    char title[MAX_TITLE_LEN];
    char artist[MAX_ARTIST_LEN];
    uint32_t duration_seconds;
    uint32_t file_size;
    bool has_id3;
} track_info_t;

/**
 * @brief Initialize track database
 * 
 * @return true on success, false on failure
 */
bool track_db_init(void);

/**
 * @brief Scan storage for MP3 files and build database
 * 
 * @return Number of tracks found
 */
uint32_t track_db_scan(void);

/**
 * @brief Get number of tracks in database
 * 
 * @return Number of tracks
 */
uint32_t track_db_get_count(void);

/**
 * @brief Get track information by index
 * 
 * @param index Track index (0 to count-1)
 * @param info Pointer to track_info_t structure to fill
 * @return true on success, false on failure
 */
bool track_db_get_track(uint32_t index, track_info_t *info);

/**
 * @brief Find track by filename
 * 
 * @param filename Track filename
 * @param info Pointer to track_info_t structure to fill
 * @return true if found, false otherwise
 */
bool track_db_find_by_filename(const char *filename, track_info_t *info);

/**
 * @brief Clear track database
 */
void track_db_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_DB_H */

