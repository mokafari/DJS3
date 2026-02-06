/**
 * @file track_history.h
 * @brief Track play history tracking with persistence
 * 
 * Provides:
 * - Recently played tracks list (configurable max size)
 * - Play count tracking per track
 * - Last played timestamp per track
 * - Persistence to filesystem
 * - Query APIs for history browsing
 * 
 * Storage is memory-efficient: uses track path hashes instead of full metadata.
 */

#ifndef TRACK_HISTORY_H
#define TRACK_HISTORY_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of tracks in recent history (circular buffer) */
#define TRACK_HISTORY_MAX_RECENT    50

/** @brief Maximum unique tracks to track play counts for */
#define TRACK_HISTORY_MAX_TRACKED   200

/** @brief History file path (stored alongside .odk metadata) */
#define TRACK_HISTORY_FILE          "/sdcard/.opendeck/history.bin"

/** @brief History file magic number "THv1" */
#define TRACK_HISTORY_MAGIC         0x54487631

/** @brief Current history file version */
#define TRACK_HISTORY_VERSION       1

/** @brief Maximum filename length stored in history entry */
#define TRACK_HISTORY_PATH_LEN      128

/**
 * @brief Single track history entry
 * 
 * Memory-efficient storage: uses truncated path instead of full metadata.
 * Full track info can be retrieved via track_db_find_by_filename().
 */
typedef struct {
    uint32_t path_hash;                     ///< FNV-1a hash of full file path
    char     path_short[TRACK_HISTORY_PATH_LEN]; ///< Truncated path for display/lookup
    uint32_t play_count;                    ///< Number of times track was played
    time_t   last_played;                   ///< Unix timestamp of last play
    time_t   first_played;                  ///< Unix timestamp of first play
} track_history_entry_t;

/**
 * @brief Query result for history searches
 */
typedef struct {
    track_history_entry_t *entries;         ///< Array of matching entries
    uint32_t count;                         ///< Number of entries in result
    uint32_t capacity;                      ///< Allocated capacity
} track_history_result_t;

/**
 * @brief Sort order for history queries
 */
typedef enum {
    HISTORY_SORT_RECENT,                    ///< Most recently played first
    HISTORY_SORT_MOST_PLAYED,               ///< Highest play count first
    HISTORY_SORT_LEAST_PLAYED,              ///< Lowest play count first
    HISTORY_SORT_OLDEST                     ///< Oldest played first
} track_history_sort_t;

/**
 * @brief Initialize track history system
 * 
 * Loads existing history from persistent storage if available.
 * 
 * @return true on success, false on failure
 */
bool track_history_init(void);

/**
 * @brief Deinitialize track history system
 * 
 * Saves current history to persistent storage and frees memory.
 */
void track_history_deinit(void);

/**
 * @brief Record a track play event
 * 
 * Updates play count, last_played timestamp, and adds to recent list.
 * Should be called when a track starts playing (not just loaded).
 * 
 * @param filepath Full path to the track file
 * @return true on success, false on failure
 */
bool track_history_record_play(const char *filepath);

/**
 * @brief Get recently played tracks
 * 
 * Returns tracks in order of most recently played first.
 * 
 * @param entries Array to fill with history entries
 * @param max_count Maximum entries to return
 * @return Number of entries filled
 */
uint32_t track_history_get_recent(track_history_entry_t *entries, uint32_t max_count);

/**
 * @brief Get most played tracks
 * 
 * Returns tracks sorted by play count (highest first).
 * 
 * @param entries Array to fill with history entries
 * @param max_count Maximum entries to return
 * @return Number of entries filled
 */
uint32_t track_history_get_most_played(track_history_entry_t *entries, uint32_t max_count);

/**
 * @brief Get history entry for a specific track
 * 
 * @param filepath Full path to the track file
 * @param entry Pointer to store the entry (if found)
 * @return true if track found in history, false otherwise
 */
bool track_history_get_track(const char *filepath, track_history_entry_t *entry);

/**
 * @brief Search history by partial path match
 * 
 * @param query Search string to match against paths
 * @param entries Array to fill with matching entries
 * @param max_count Maximum entries to return
 * @return Number of matching entries
 */
uint32_t track_history_search(const char *query, track_history_entry_t *entries, uint32_t max_count);

/**
 * @brief Get play count for a specific track
 * 
 * @param filepath Full path to the track file
 * @return Play count (0 if not in history)
 */
uint32_t track_history_get_play_count(const char *filepath);

/**
 * @brief Get total number of unique tracks in history
 * 
 * @return Number of tracked tracks
 */
uint32_t track_history_get_total_tracks(void);

/**
 * @brief Get total play count across all tracks
 * 
 * @return Sum of all play counts
 */
uint32_t track_history_get_total_plays(void);

/**
 * @brief Clear all history data
 * 
 * Removes all entries from memory and persistent storage.
 * 
 * @return true on success, false on failure
 */
bool track_history_clear(void);

/**
 * @brief Remove a specific track from history
 * 
 * @param filepath Full path to the track file
 * @return true if removed, false if not found
 */
bool track_history_remove_track(const char *filepath);

/**
 * @brief Force save history to persistent storage
 * 
 * Normally history is saved automatically on changes.
 * This function can be used to ensure immediate persistence.
 * 
 * @return true on success, false on failure
 */
bool track_history_save(void);

/**
 * @brief Check if history has been modified since last save
 * 
 * @return true if there are unsaved changes
 */
bool track_history_is_dirty(void);

/**
 * @brief Get number of recent plays (within time window)
 * 
 * @param hours Number of hours to look back
 * @return Number of plays within the time window
 */
uint32_t track_history_get_plays_since(uint32_t hours);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_HISTORY_H */
