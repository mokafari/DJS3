/**
 * @file analyzer.h
 * @brief Track analyzer API with two-pass PSRAM-safe analysis
 * 
 * Implements a two-pass analysis strategy:
 * - Pass 1 (Fast): Scan MP3 headers for seek table and duration
 *   - Safe to run during playback (minimal PSRAM use)
 *   - Enables immediate seeking
 * 
 * - Pass 2 (Slow): Decode PCM for waveform and BPM detection
 *   - ONLY runs when playback is paused/stopped
 *   - Prevents PSRAM bus contention that causes audio glitches
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Analyzer state machine states
 */
typedef enum {
    ANALYZER_STATE_IDLE,           ///< No analysis in progress
    ANALYZER_STATE_PASS1_RUNNING,  ///< Scanning MP3 headers
    ANALYZER_STATE_PASS2_PENDING,  ///< Waiting for playback to stop
    ANALYZER_STATE_PASS2_RUNNING,  ///< Decoding PCM for waveform/BPM
    ANALYZER_STATE_PRE_ANALYZING   ///< Pre-analyzing upcoming tracks
} analyzer_state_t;

/**
 * @brief Analysis progress callback
 * 
 * @param filepath  File being analyzed
 * @param progress  Progress percentage (0-100)
 * @param pass      Current pass (1 or 2)
 */
typedef void (*analyzer_progress_cb_t)(const char *filepath, int progress, int pass);

/**
 * @brief Initialize analyzer subsystem
 * 
 * Creates tasks and queues for background analysis.
 * Must be called before any other analyzer functions.
 */
void analyzer_init(void);

/**
 * @brief Start analysis for a track
 * 
 * Spawns Pass 1 task immediately. Pass 2 will be queued
 * and run when playback stops.
 * 
 * @param filepath  Path to MP3 file
 */
void analyzer_start(const char *filepath);

/**
 * @brief Suspend analyzer (called when playback starts)
 * 
 * Immediately suspends Pass 2 if running to prevent
 * PSRAM bus contention with audio playback.
 */
void analyzer_suspend(void);

/**
 * @brief Resume analyzer (called when playback stops)
 * 
 * Allows Pass 2 to run if pending.
 */
void analyzer_resume(void);

/**
 * @brief Cancel any running analysis
 * 
 * Stops both Pass 1 and Pass 2, clears queues.
 */
void analyzer_cancel(void);

/**
 * @brief Get current analyzer state
 * 
 * @return Current analyzer_state_t
 */
analyzer_state_t analyzer_get_state(void);

/**
 * @brief Check if analysis is complete for a track
 * 
 * @param filepath  Path to MP3 file
 * @return true if both passes complete, false otherwise
 */
bool analyzer_is_complete(const char *filepath);

/**
 * @brief Queue track for pre-analysis
 * 
 * Adds track to pre-analysis queue. Pre-analysis only runs
 * when deck is completely idle (stopped, no track loaded).
 * 
 * @param filepath  Path to MP3 file
 */
void analyzer_queue_preanalysis(const char *filepath);

/**
 * @brief Set progress callback
 * 
 * @param callback  Progress callback function (NULL to disable)
 */
void analyzer_set_progress_callback(analyzer_progress_cb_t callback);

/**
 * @brief Get number of tracks in pre-analysis queue
 * 
 * @return Number of queued tracks
 */
int analyzer_get_queue_count(void);

#ifdef __cplusplus
}
#endif
