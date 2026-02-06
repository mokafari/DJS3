/**
 * @file track_prep.h
 * @brief Track preparation and pre-gig analysis system
 * 
 * Provides batch analysis UI and library verification for DJs
 * to prepare their track collection before a performance.
 * 
 * Features:
 * - Batch analysis with progress UI
 * - Pre-gig library verification
 * - Missing .odk file detection
 * - Orphaned metadata cleanup
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum tracks that can be queued for preparation
#define TRACK_PREP_MAX_QUEUE        500

// Maximum missing/problem files to track
#define TRACK_PREP_MAX_ISSUES       100

// Preparation scan modes
typedef enum {
    TRACK_PREP_MODE_QUICK,      ///< Only check for missing .odk files
    TRACK_PREP_MODE_FULL,       ///< Analyze all unanalyzed tracks
    TRACK_PREP_MODE_VERIFY,     ///< Verify existing .odk files match source
    TRACK_PREP_MODE_PLAYLIST,   ///< Prepare specific playlist
} track_prep_mode_t;

// Preparation status
typedef enum {
    TRACK_PREP_STATUS_IDLE,         ///< Not running
    TRACK_PREP_STATUS_SCANNING,     ///< Scanning for tracks
    TRACK_PREP_STATUS_ANALYZING,    ///< Analyzing tracks
    TRACK_PREP_STATUS_VERIFYING,    ///< Verifying metadata
    TRACK_PREP_STATUS_COMPLETE,     ///< Preparation complete
    TRACK_PREP_STATUS_CANCELLED,    ///< Cancelled by user
    TRACK_PREP_STATUS_ERROR,        ///< Error occurred
} track_prep_status_t;

// Issue types for problem tracks
typedef enum {
    TRACK_ISSUE_MISSING_ODK,        ///< MP3 exists but no .odk metadata
    TRACK_ISSUE_ORPHAN_ODK,         ///< .odk exists but source MP3 missing
    TRACK_ISSUE_SIZE_MISMATCH,      ///< Source file size changed since analysis
    TRACK_ISSUE_CORRUPT_ODK,        ///< .odk file is corrupt/unreadable
    TRACK_ISSUE_ANALYSIS_FAILED,    ///< Analysis failed for this track
} track_issue_type_t;

/**
 * @brief Track issue descriptor
 */
typedef struct {
    char path[256];                 ///< Path to problematic file
    track_issue_type_t type;        ///< Type of issue
    uint32_t source_size;           ///< Current file size (for mismatch)
    uint32_t odk_size;              ///< Recorded size in .odk
} track_issue_t;

/**
 * @brief Preparation statistics
 */
typedef struct {
    uint32_t total_mp3s;            ///< Total MP3 files found
    uint32_t already_analyzed;      ///< Tracks with valid .odk
    uint32_t needs_analysis;        ///< Tracks needing analysis
    uint32_t analyzed_now;          ///< Tracks analyzed this session
    uint32_t failed;                ///< Analysis failures
    uint32_t issues_found;          ///< Total issues detected
    uint32_t orphan_odks;           ///< Orphaned .odk files
    uint32_t size_mismatches;       ///< Files changed since analysis
} track_prep_stats_t;

/**
 * @brief Progress callback for UI updates
 * 
 * @param status    Current preparation status
 * @param phase     Description of current phase (e.g., "Scanning Music...")
 * @param current   Current item number
 * @param total     Total items to process
 * @param percent   Overall progress percentage (0-100)
 * @param user_data User data from track_prep_set_callback()
 */
typedef void (*track_prep_progress_cb)(
    track_prep_status_t status,
    const char *phase,
    uint32_t current,
    uint32_t total,
    int percent,
    void *user_data
);

/**
 * @brief Initialize track preparation system
 * 
 * Must be called before other track_prep functions.
 * Safe to call multiple times (idempotent).
 * 
 * @return true on success
 */
bool track_prep_init(void);

/**
 * @brief Start track preparation
 * 
 * Begins scanning and optionally analyzing tracks in background.
 * Progress is reported via callback.
 * 
 * @param mode      Preparation mode
 * @param path      Base path to scan (NULL for default /sdcard/Music)
 * @return true if started, false if already running
 */
bool track_prep_start(track_prep_mode_t mode, const char *path);

/**
 * @brief Start preparation for a specific playlist
 * 
 * Prepares only tracks in the given playlist.
 * 
 * @param playlist_path  Path to .m3u playlist file
 * @return true if started
 */
bool track_prep_start_playlist(const char *playlist_path);

/**
 * @brief Cancel current preparation
 * 
 * Stops after current track completes.
 */
void track_prep_cancel(void);

/**
 * @brief Check if preparation is running
 * 
 * @return true if running
 */
bool track_prep_is_running(void);

/**
 * @brief Get current preparation status
 * 
 * @return Current status
 */
track_prep_status_t track_prep_get_status(void);

/**
 * @brief Get current progress percentage
 * 
 * @return Progress 0-100
 */
int track_prep_get_progress(void);

/**
 * @brief Get preparation statistics
 * 
 * @param stats  Pointer to stats structure to fill
 */
void track_prep_get_stats(track_prep_stats_t *stats);

/**
 * @brief Get current phase description
 * 
 * @return Static string describing current phase
 */
const char* track_prep_get_phase(void);

/**
 * @brief Get number of issues found
 * 
 * @return Issue count
 */
uint32_t track_prep_get_issue_count(void);

/**
 * @brief Get issue by index
 * 
 * @param index  Issue index (0 to count-1)
 * @param issue  Pointer to issue structure to fill
 * @return true if valid index
 */
bool track_prep_get_issue(uint32_t index, track_issue_t *issue);

/**
 * @brief Set progress callback
 * 
 * @param cb         Callback function (NULL to disable)
 * @param user_data  User data passed to callback
 */
void track_prep_set_callback(track_prep_progress_cb cb, void *user_data);

/**
 * @brief Resolve a single issue
 * 
 * For missing .odk: queues track for analysis
 * For orphaned .odk: deletes the orphan file
 * For size mismatch: re-analyzes the track
 * 
 * @param index  Issue index to resolve
 * @return true if resolution started
 */
bool track_prep_resolve_issue(uint32_t index);

/**
 * @brief Resolve all issues of a given type
 * 
 * @param type  Issue type to resolve
 * @return Number of issues queued for resolution
 */
uint32_t track_prep_resolve_all(track_issue_type_t type);

/**
 * @brief Quick scan to count unanalyzed tracks
 * 
 * Lightweight scan that only counts files.
 * Does not start analysis.
 * 
 * @param path  Base path to scan (NULL for default)
 * @param stats Output statistics
 * @return true on success
 */
bool track_prep_quick_scan(const char *path, track_prep_stats_t *stats);

/**
 * @brief Check if a specific track needs analysis
 * 
 * @param mp3_path  Path to MP3 file
 * @return true if .odk is missing or outdated
 */
bool track_prep_needs_analysis(const char *mp3_path);

/**
 * @brief Get estimated time remaining
 * 
 * @return Estimated seconds remaining, or 0 if unknown
 */
uint32_t track_prep_get_eta(void);

/**
 * @brief Clean up orphaned .odk files
 * 
 * Removes .odk files whose source MP3s no longer exist.
 * 
 * @param dry_run  If true, only count without deleting
 * @return Number of orphans found/deleted
 */
uint32_t track_prep_cleanup_orphans(bool dry_run);

#ifdef __cplusplus
}
#endif
