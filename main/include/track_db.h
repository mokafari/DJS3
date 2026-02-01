/**
 * @file track_db.h
 * @brief Track database with ID3 tag parsing, BPM/key from .odk metadata
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
 * 
 * Extended to include BPM and key from .odk metadata files.
 */
typedef struct {
    char filename[MAX_FILENAME_LEN];
    char title[MAX_TITLE_LEN];
    char artist[MAX_ARTIST_LEN];
    uint32_t duration_seconds;
    uint32_t file_size;
    bool has_id3;
    
    // From .odk metadata (0 if not analyzed)
    float bpm;              ///< Beats per minute (e.g., 128.0)
    uint8_t key_id;         ///< Camelot key (0-23, or 255 if unknown)
    bool has_metadata;      ///< True if .odk file exists with analysis
    bool has_cues;          ///< True if track has hot cues set
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

/**
 * @brief Sort tracks by BPM
 * 
 * @param ascending true for low-to-high, false for high-to-low
 */
void track_db_sort_by_bpm(bool ascending);

/**
 * @brief Sort tracks by key (Camelot order)
 * 
 * @param ascending true for 1A-12B, false for 12B-1A
 */
void track_db_sort_by_key(bool ascending);

/**
 * @brief Sort tracks by title (alphabetical)
 * 
 * @param ascending true for A-Z, false for Z-A
 */
void track_db_sort_by_title(bool ascending);

/**
 * @brief Get key name string for a key ID
 * 
 * @param key_id Camelot key (0-23)
 * @return Key name string (e.g., "8A", "5B") or "?" if unknown
 */
const char* track_db_get_key_name(uint8_t key_id);

/**
 * @brief Refresh metadata for all tracks
 * 
 * Re-scans .odk files to update BPM/key/cue info.
 */
void track_db_refresh_metadata(void);

/**
 * @brief Queue nearby tracks for pre-analysis
 * 
 * Should be called when user browses to a folder/position.
 * 
 * @param center_index Current browse position
 */
void track_db_queue_preanalysis(uint32_t center_index);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_DB_H */

