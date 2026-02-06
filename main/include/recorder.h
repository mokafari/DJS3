/**
 * @file recorder.h
 * @brief Master output recorder for capturing mixed audio to SD card
 * 
 * Records the master output to WAV files on the SD card with:
 * - 16-bit 44.1kHz stereo format
 * - Auto-split on track change
 * - Real-time level metering
 */

#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

/** @brief Sample rate for recordings (Hz) */
#define RECORDER_SAMPLE_RATE    44100

/** @brief Number of channels (stereo) */
#define RECORDER_CHANNELS       2

/** @brief Bits per sample */
#define RECORDER_BITS_PER_SAMPLE 16

/** @brief Recording buffer size in stereo frames */
#define RECORDER_BUFFER_FRAMES  4096

/** @brief Maximum filename length */
#define RECORDER_MAX_FILENAME   256

/** @brief Default recordings directory on SD card */
#define RECORDER_DEFAULT_DIR    "/sdcard/recordings"

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Recorder state
 */
typedef enum {
    RECORDER_STATE_IDLE = 0,      ///< Not recording, ready to start
    RECORDER_STATE_RECORDING,     ///< Actively recording
    RECORDER_STATE_PAUSED,        ///< Recording paused (can resume)
    RECORDER_STATE_ERROR          ///< Error state (SD card full, etc.)
} recorder_state_t;

/**
 * @brief Recorder statistics
 */
typedef struct {
    uint32_t duration_ms;         ///< Current recording duration (milliseconds)
    uint32_t bytes_written;       ///< Total bytes written to current file
    uint32_t files_created;       ///< Number of files created in session
    uint32_t buffer_overruns;     ///< Count of buffer overruns (lost samples)
    float disk_usage_mb;          ///< Approximate disk usage (MB)
} recorder_stats_t;

/**
 * @brief Level meter values (0.0 to 1.0, peak hold)
 */
typedef struct {
    float left_peak;              ///< Left channel peak level
    float right_peak;             ///< Right channel peak level
    float left_rms;               ///< Left channel RMS level
    float right_rms;              ///< Right channel RMS level
    bool clipping_left;           ///< Left channel clipping detected
    bool clipping_right;          ///< Right channel clipping detected
} recorder_levels_t;

/**
 * @brief Callback for recording events
 */
typedef void (*recorder_event_cb_t)(recorder_state_t state, const char *filename);

// ============================================================================
// Lifecycle
// ============================================================================

/**
 * @brief Initialize the recorder subsystem
 * 
 * Creates the recording directory if it doesn't exist.
 * Must be called after SD card is mounted.
 * 
 * @return true on success, false on failure
 */
bool recorder_init(void);

/**
 * @brief Deinitialize the recorder subsystem
 * 
 * Stops any active recording and frees resources.
 */
void recorder_deinit(void);

/**
 * @brief Check if recorder is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool recorder_is_initialized(void);

// ============================================================================
// Recording Control
// ============================================================================

/**
 * @brief Start recording
 * 
 * Creates a new WAV file with timestamp-based filename.
 * If already recording, does nothing.
 * 
 * @return true on success, false on failure
 */
bool recorder_start(void);

/**
 * @brief Start recording with custom filename
 * 
 * @param filename Filename (without path or extension)
 * @return true on success, false on failure
 */
bool recorder_start_with_name(const char *filename);

/**
 * @brief Stop recording
 * 
 * Finalizes the WAV header and closes the file.
 */
void recorder_stop(void);

/**
 * @brief Pause recording
 * 
 * Temporarily stops writing samples but keeps file open.
 */
void recorder_pause(void);

/**
 * @brief Resume recording after pause
 */
void recorder_resume(void);

/**
 * @brief Get current recorder state
 * 
 * @return Current state
 */
recorder_state_t recorder_get_state(void);

/**
 * @brief Check if recording is active
 * 
 * @return true if recording or paused, false if idle
 */
bool recorder_is_recording(void);

// ============================================================================
// Track Split (Auto-split feature)
// ============================================================================

/**
 * @brief Enable or disable auto-split on track change
 * 
 * When enabled, a new file is created each time recorder_split() is called.
 * 
 * @param enable true to enable, false to disable
 */
void recorder_set_auto_split(bool enable);

/**
 * @brief Check if auto-split is enabled
 * 
 * @return true if enabled
 */
bool recorder_get_auto_split(void);

/**
 * @brief Split recording to a new file
 * 
 * Call this when a new track starts playing.
 * If auto-split is disabled, this does nothing.
 * If not currently recording, this does nothing.
 * 
 * @param track_name Optional track name for the new file (can be NULL)
 */
void recorder_split(const char *track_name);

// ============================================================================
// Audio Feed (called from playback task)
// ============================================================================

/**
 * @brief Feed audio samples to the recorder
 * 
 * This should be called from the audio playback task with the mixed
 * master output samples. Thread-safe (uses internal buffering).
 * 
 * @param samples Interleaved stereo 16-bit samples
 * @param num_frames Number of stereo frames (samples / 2)
 */
void recorder_feed_samples(const int16_t *samples, size_t num_frames);

// ============================================================================
// Level Meter
// ============================================================================

/**
 * @brief Get current recording levels
 * 
 * Returns peak and RMS levels for display. Values are smoothed
 * and suitable for UI display at 30-60 Hz update rate.
 * 
 * @param levels Output structure for level values
 */
void recorder_get_levels(recorder_levels_t *levels);

/**
 * @brief Reset level meters
 * 
 * Clears peak hold and clipping indicators.
 */
void recorder_reset_levels(void);

/**
 * @brief Set level meter peak hold time
 * 
 * @param hold_ms Peak hold time in milliseconds (0 = no hold)
 */
void recorder_set_peak_hold(uint32_t hold_ms);

// ============================================================================
// Statistics
// ============================================================================

/**
 * @brief Get recording statistics
 * 
 * @param stats Output structure for statistics
 */
void recorder_get_stats(recorder_stats_t *stats);

/**
 * @brief Get current recording filename
 * 
 * @return Full path to current recording file, or NULL if not recording
 */
const char* recorder_get_current_file(void);

/**
 * @brief Get estimated remaining recording time
 * 
 * Based on available SD card space.
 * 
 * @return Remaining time in seconds, or 0 if unknown
 */
uint32_t recorder_get_remaining_time(void);

// ============================================================================
// Event Callbacks
// ============================================================================

/**
 * @brief Set callback for recording events
 * 
 * Callback is invoked on state changes and file splits.
 * 
 * @param callback Event callback function (NULL to disable)
 */
void recorder_set_event_callback(recorder_event_cb_t callback);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Set recording directory
 * 
 * @param path Directory path on SD card
 * @return true on success, false on failure
 */
bool recorder_set_directory(const char *path);

/**
 * @brief Get recording directory
 * 
 * @return Current recording directory path
 */
const char* recorder_get_directory(void);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_H */
