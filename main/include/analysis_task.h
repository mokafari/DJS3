/**
 * @file analysis_task.h
 * @brief Background track analysis task
 * 
 * FreeRTOS task that orchestrates BPM detection, waveform generation,
 * and key detection in the background. Runs on Core 0 to avoid
 * interfering with real-time audio playback on Core 1.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Analysis progress callback
 * 
 * Called during analysis to report progress to UI or other systems.
 * 
 * @param filename   Currently analyzing filename (basename only)
 * @param percent    Progress percentage (0-100)
 * @param user_data  User data pointer from analysis_task_set_callback()
 */
typedef void (*analysis_progress_cb)(const char *filename, int percent, void *user_data);

/**
 * @brief Initialize the analysis task
 * 
 * Creates FreeRTOS task on Core 0 with low priority.
 * Safe to call multiple times (idempotent).
 * 
 * @return true if initialized (or already running), false on error
 */
bool analysis_task_init(void);

/**
 * @brief Queue a track for background analysis
 * 
 * Adds track to analysis queue. Skips if .odk already exists.
 * 
 * @param mp3_path  Full path to MP3 file on SD card
 * @return true if queued (or already analyzed), false if queue full
 */
bool analysis_task_queue(const char *mp3_path);

/**
 * @brief Queue multiple tracks for analysis
 * 
 * Convenience function for batch operations (e.g., entire folder).
 * 
 * @param paths  Array of MP3 file paths
 * @param count  Number of paths in array
 * @return Number of tracks successfully queued
 */
int analysis_task_queue_batch(const char **paths, int count);

/**
 * @brief Get current queue depth
 * 
 * @return Number of tracks waiting to be analyzed
 */
int analysis_task_get_queue_depth(void);

/**
 * @brief Check if currently analyzing a track
 * 
 * @return true if analysis is in progress
 */
bool analysis_task_is_busy(void);

/**
 * @brief Get current analysis progress
 * 
 * @return Progress percentage (0-100)
 */
int analysis_task_get_progress(void);

/**
 * @brief Get currently analyzing filename
 * 
 * @return Filename (path) being analyzed, or NULL if idle
 */
const char* analysis_task_get_current_file(void);

/**
 * @brief Set progress callback
 * 
 * @param cb         Callback function (NULL to disable)
 * @param user_data  User data passed to callback
 */
void analysis_task_set_callback(analysis_progress_cb cb, void *user_data);

/**
 * @brief Cancel current analysis and clear queue
 * 
 * Immediately stops current analysis and empties the queue.
 */
void analysis_task_cancel(void);

/**
 * @brief Pause analysis task
 * 
 * Suspends analysis after current track completes.
 * Use to temporarily free CPU for intensive operations.
 */
void analysis_task_pause(void);

/**
 * @brief Resume analysis task
 * 
 * Resumes analysis after pause.
 */
void analysis_task_resume(void);

/**
 * @brief Check if analysis is paused
 * 
 * @return true if paused
 */
bool analysis_task_is_paused(void);

#ifdef __cplusplus
}
#endif
