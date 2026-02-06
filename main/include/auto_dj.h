/**
 * @file auto_dj.h
 * @brief Auto-DJ system for automated playback with intelligent mixing
 * 
 * Provides:
 * - Queue management (up next list with add/remove/reorder)
 * - Auto-crossfade at track end (configurable fade time and curve)
 * - BPM matching for smooth tempo transitions
 * - Key-compatible track selection hints (Camelot wheel)
 * - Integration with track_history to avoid recent repeats
 * 
 * The Auto-DJ manages automated playback between two virtual decks,
 * with crossfade transitions that respect BPM and key relationships.
 */

#ifndef AUTO_DJ_H
#define AUTO_DJ_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

/** @brief Maximum tracks in the Auto-DJ queue */
#define AUTO_DJ_QUEUE_MAX           100

/** @brief Maximum path length for queue entries */
#define AUTO_DJ_PATH_MAX            256

/** @brief Default crossfade time in milliseconds */
#define AUTO_DJ_DEFAULT_CROSSFADE_MS 8000

/** @brief Minimum crossfade time in milliseconds */
#define AUTO_DJ_MIN_CROSSFADE_MS    1000

/** @brief Maximum crossfade time in milliseconds */
#define AUTO_DJ_MAX_CROSSFADE_MS    30000

/** @brief BPM tolerance for "compatible" match (percentage) */
#define AUTO_DJ_BPM_TOLERANCE_PERCENT 6.0f

/** @brief Maximum BPM adjustment allowed (percentage) */
#define AUTO_DJ_MAX_BPM_ADJUST_PERCENT 8.0f

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief Auto-DJ operating state
 */
typedef enum {
    AUTO_DJ_STATE_DISABLED = 0,     ///< Auto-DJ is off
    AUTO_DJ_STATE_IDLE,             ///< Enabled but queue empty
    AUTO_DJ_STATE_PLAYING,          ///< Normal playback on active deck
    AUTO_DJ_STATE_CROSSFADING,      ///< Crossfade transition in progress
    AUTO_DJ_STATE_PAUSED            ///< Temporarily paused
} auto_dj_state_t;

/**
 * @brief Crossfade curve types
 */
typedef enum {
    AUTO_DJ_CURVE_LINEAR = 0,       ///< Linear fade (constant power)
    AUTO_DJ_CURVE_EQUAL_POWER,      ///< Equal power curve (smooth)
    AUTO_DJ_CURVE_SLOW_CUT,         ///< Slow fade out, quick fade in
    AUTO_DJ_CURVE_FAST_CUT,         ///< Quick fade out, slow fade in
    AUTO_DJ_CURVE_HARD_CUT          ///< Instant switch at midpoint
} auto_dj_curve_t;

/**
 * @brief Track selection mode for auto-queue
 */
typedef enum {
    AUTO_DJ_SELECT_SEQUENTIAL = 0,  ///< Play in order
    AUTO_DJ_SELECT_SHUFFLE,         ///< Random order
    AUTO_DJ_SELECT_SMART            ///< BPM/key-aware selection
} auto_dj_select_mode_t;

/**
 * @brief Key compatibility level
 */
typedef enum {
    KEY_COMPAT_NONE = 0,            ///< Keys are incompatible
    KEY_COMPAT_SAME,                ///< Same key (perfect match)
    KEY_COMPAT_ADJACENT,            ///< +/- 1 on Camelot wheel
    KEY_COMPAT_RELATIVE,            ///< Relative major/minor
    KEY_COMPAT_ENERGY_BOOST,        ///< +7 on wheel (energy boost)
    KEY_COMPAT_ENERGY_DROP          ///< -7 on wheel (energy drop)
} key_compat_t;

/**
 * @brief Queue entry structure
 */
typedef struct {
    char     filepath[AUTO_DJ_PATH_MAX];  ///< Full path to track
    char     title[64];                    ///< Display title (from ID3 or filename)
    float    bpm;                          ///< Track BPM (0 if unknown)
    uint8_t  key_id;                       ///< Camelot key ID (0-23, 255 if unknown)
    uint32_t duration_ms;                  ///< Track duration in milliseconds
    bool     analyzed;                     ///< True if BPM/key data is available
} auto_dj_queue_entry_t;

/**
 * @brief Track suggestion with compatibility info
 */
typedef struct {
    char     filepath[AUTO_DJ_PATH_MAX];  ///< Path to suggested track
    char     title[64];                    ///< Display title
    float    bpm;                          ///< Track BPM
    uint8_t  key_id;                       ///< Camelot key ID
    float    bpm_diff_percent;             ///< BPM difference from current (%)
    key_compat_t key_compat;               ///< Key compatibility level
    uint32_t score;                        ///< Overall compatibility score (higher = better)
} auto_dj_suggestion_t;

/**
 * @brief Auto-DJ configuration
 */
typedef struct {
    uint32_t            crossfade_ms;      ///< Crossfade duration
    auto_dj_curve_t     curve;             ///< Crossfade curve type
    auto_dj_select_mode_t select_mode;     ///< Track selection mode
    bool                bpm_sync_enabled;  ///< Enable BPM matching during crossfade
    bool                key_lock_enabled;  ///< Enable key lock during BPM sync
    float               max_bpm_adjust;    ///< Maximum BPM adjustment (percent)
    bool                avoid_recent;      ///< Avoid recently played tracks
    uint32_t            recent_hours;      ///< Hours to consider as "recent"
} auto_dj_config_t;

/**
 * @brief Auto-DJ event types for callbacks
 */
typedef enum {
    AUTO_DJ_EVENT_STARTED,          ///< Auto-DJ has been enabled
    AUTO_DJ_EVENT_STOPPED,          ///< Auto-DJ has been disabled
    AUTO_DJ_EVENT_TRACK_LOADED,     ///< New track loaded on deck
    AUTO_DJ_EVENT_CROSSFADE_START,  ///< Crossfade has started
    AUTO_DJ_EVENT_CROSSFADE_END,    ///< Crossfade completed
    AUTO_DJ_EVENT_QUEUE_EMPTY,      ///< Queue ran out of tracks
    AUTO_DJ_EVENT_QUEUE_LOW,        ///< Queue is running low (<5 tracks)
    AUTO_DJ_EVENT_ERROR             ///< Error occurred
} auto_dj_event_t;

/**
 * @brief Event callback function type
 * 
 * @param event Event type
 * @param data  Event-specific data (usually filepath or error message)
 */
typedef void (*auto_dj_event_cb_t)(auto_dj_event_t event, const char *data);

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize Auto-DJ system
 * 
 * Must be called before any other Auto-DJ functions.
 * 
 * @return true on success, false on failure
 */
bool auto_dj_init(void);

/**
 * @brief Deinitialize Auto-DJ system
 * 
 * Stops Auto-DJ if running and frees resources.
 */
void auto_dj_deinit(void);

/**
 * @brief Set event callback
 * 
 * @param callback Callback function (NULL to disable)
 */
void auto_dj_set_event_callback(auto_dj_event_cb_t callback);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Get current configuration
 * 
 * @param config Pointer to store configuration
 */
void auto_dj_get_config(auto_dj_config_t *config);

/**
 * @brief Set configuration
 * 
 * @param config New configuration to apply
 * @return true on success
 */
bool auto_dj_set_config(const auto_dj_config_t *config);

/**
 * @brief Get default configuration
 * 
 * @param config Pointer to fill with defaults
 */
void auto_dj_get_default_config(auto_dj_config_t *config);

/**
 * @brief Set crossfade time
 * 
 * @param ms Crossfade duration in milliseconds
 * @return true on success
 */
bool auto_dj_set_crossfade_time(uint32_t ms);

/**
 * @brief Get crossfade time
 * 
 * @return Crossfade duration in milliseconds
 */
uint32_t auto_dj_get_crossfade_time(void);

/**
 * @brief Set crossfade curve
 * 
 * @param curve Curve type to use
 */
void auto_dj_set_curve(auto_dj_curve_t curve);

/**
 * @brief Set track selection mode
 * 
 * @param mode Selection mode
 */
void auto_dj_set_select_mode(auto_dj_select_mode_t mode);

// ============================================================================
// Control
// ============================================================================

/**
 * @brief Enable Auto-DJ
 * 
 * Starts automated playback. If queue is empty, waits for tracks.
 * If queue has tracks, begins playback immediately.
 * 
 * @return true on success
 */
bool auto_dj_enable(void);

/**
 * @brief Disable Auto-DJ
 * 
 * Stops automated control but keeps current track playing.
 */
void auto_dj_disable(void);

/**
 * @brief Check if Auto-DJ is enabled
 * 
 * @return true if enabled
 */
bool auto_dj_is_enabled(void);

/**
 * @brief Get current state
 * 
 * @return Current Auto-DJ state
 */
auto_dj_state_t auto_dj_get_state(void);

/**
 * @brief Pause Auto-DJ (keeps state, stops auto-transitions)
 */
void auto_dj_pause(void);

/**
 * @brief Resume Auto-DJ from pause
 */
void auto_dj_resume(void);

/**
 * @brief Skip to next track immediately
 * 
 * Triggers an immediate crossfade to the next track.
 * 
 * @return true if skip was initiated
 */
bool auto_dj_skip(void);

/**
 * @brief Update Auto-DJ (call from main loop)
 * 
 * Monitors playback position and triggers crossfades.
 * Call this regularly (e.g., every 100ms) while Auto-DJ is enabled.
 */
void auto_dj_update(void);

// ============================================================================
// Queue Management
// ============================================================================

/**
 * @brief Add track to end of queue
 * 
 * @param filepath Path to track file
 * @return true on success, false if queue full
 */
bool auto_dj_queue_add(const char *filepath);

/**
 * @brief Add track at specific position
 * 
 * @param filepath Path to track file
 * @param position Insert position (0 = next up)
 * @return true on success
 */
bool auto_dj_queue_insert(const char *filepath, uint32_t position);

/**
 * @brief Add multiple tracks to queue
 * 
 * @param filepaths Array of file paths
 * @param count Number of paths in array
 * @return Number of tracks successfully added
 */
uint32_t auto_dj_queue_add_multiple(const char **filepaths, uint32_t count);

/**
 * @brief Remove track from queue by position
 * 
 * @param position Position in queue (0 = next up)
 * @return true if removed
 */
bool auto_dj_queue_remove(uint32_t position);

/**
 * @brief Remove track from queue by path
 * 
 * @param filepath Path to remove
 * @return true if found and removed
 */
bool auto_dj_queue_remove_path(const char *filepath);

/**
 * @brief Move track within queue
 * 
 * @param from_pos Current position
 * @param to_pos New position
 * @return true on success
 */
bool auto_dj_queue_move(uint32_t from_pos, uint32_t to_pos);

/**
 * @brief Clear all tracks from queue
 */
void auto_dj_queue_clear(void);

/**
 * @brief Shuffle the queue
 */
void auto_dj_queue_shuffle(void);

/**
 * @brief Get number of tracks in queue
 * 
 * @return Track count
 */
uint32_t auto_dj_queue_count(void);

/**
 * @brief Get queue entry by position
 * 
 * @param position Position in queue
 * @param entry Pointer to store entry data
 * @return true if position valid
 */
bool auto_dj_queue_get(uint32_t position, auto_dj_queue_entry_t *entry);

/**
 * @brief Get the next track that will play
 * 
 * @param entry Pointer to store entry data
 * @return true if queue has tracks
 */
bool auto_dj_queue_peek_next(auto_dj_queue_entry_t *entry);

/**
 * @brief Check if a track is in the queue
 * 
 * @param filepath Path to check
 * @return true if track is queued
 */
bool auto_dj_queue_contains(const char *filepath);

// ============================================================================
// BPM Matching
// ============================================================================

/**
 * @brief Check if two BPMs are compatible
 * 
 * Two BPMs are compatible if they are within the tolerance percentage,
 * or if one is a harmonic multiple of the other (half/double).
 * 
 * @param bpm1 First BPM
 * @param bpm2 Second BPM
 * @return true if compatible
 */
bool auto_dj_bpm_compatible(float bpm1, float bpm2);

/**
 * @brief Calculate BPM difference as percentage
 * 
 * @param from_bpm Source BPM
 * @param to_bpm Target BPM
 * @return Percentage difference (positive = faster)
 */
float auto_dj_bpm_diff_percent(float from_bpm, float to_bpm);

/**
 * @brief Calculate pitch adjustment for BPM match
 * 
 * Returns the pitch/tempo percentage adjustment needed to match
 * from_bpm to to_bpm. Considers harmonic multiples.
 * 
 * @param from_bpm Current track BPM
 * @param to_bpm Target BPM to match
 * @return Pitch adjustment (e.g., 1.05 for +5%, 0.95 for -5%)
 */
float auto_dj_calc_pitch_adjust(float from_bpm, float to_bpm);

/**
 * @brief Get the effective BPM (considering half/double)
 * 
 * Some tracks are analyzed at half or double time.
 * This returns the BPM normalized to a typical range (80-160).
 * 
 * @param bpm Raw BPM value
 * @return Normalized BPM
 */
float auto_dj_normalize_bpm(float bpm);

// ============================================================================
// Key Compatibility (Camelot Wheel)
// ============================================================================

/**
 * @brief Check key compatibility between two tracks
 * 
 * Uses Camelot wheel rules:
 * - Same key: Perfect match
 * - +/- 1 on wheel: Energy shift
 * - Inner/outer swap (A<->B): Relative major/minor
 * 
 * @param key1 First key (Camelot ID 0-23)
 * @param key2 Second key (Camelot ID 0-23)
 * @return Compatibility level
 */
key_compat_t auto_dj_key_compatibility(uint8_t key1, uint8_t key2);

/**
 * @brief Get compatible keys for a given key
 * 
 * @param key_id Source key (Camelot ID)
 * @param compat_keys Array to fill with compatible key IDs
 * @param max_keys Maximum keys to return
 * @return Number of compatible keys found
 */
uint32_t auto_dj_get_compatible_keys(uint8_t key_id, uint8_t *compat_keys, uint32_t max_keys);

/**
 * @brief Get Camelot key name
 * 
 * @param key_id Key ID (0-23)
 * @return Key name string (e.g., "8A", "11B") or "?" if invalid
 */
const char* auto_dj_get_key_name(uint8_t key_id);

// ============================================================================
// Track Suggestions
// ============================================================================

/**
 * @brief Get suggested tracks based on current playback
 * 
 * Returns tracks from the library sorted by compatibility with
 * the currently playing track (BPM + key matching).
 * 
 * @param suggestions Array to fill with suggestions
 * @param max_count Maximum suggestions to return
 * @param current_bpm Current track BPM (or 0 to ignore)
 * @param current_key Current track key ID (or 255 to ignore)
 * @return Number of suggestions found
 */
uint32_t auto_dj_get_suggestions(auto_dj_suggestion_t *suggestions, uint32_t max_count,
                                   float current_bpm, uint8_t current_key);

/**
 * @brief Calculate compatibility score between two tracks
 * 
 * Higher score = better match. Considers:
 * - BPM difference
 * - Key compatibility
 * - Recently played penalty
 * 
 * @param bpm1 First track BPM
 * @param key1 First track key
 * @param bpm2 Second track BPM
 * @param key2 Second track key
 * @return Compatibility score (0-1000)
 */
uint32_t auto_dj_calc_compatibility(float bpm1, uint8_t key1, float bpm2, uint8_t key2);

// ============================================================================
// Crossfade Control
// ============================================================================

/**
 * @brief Get current crossfade progress
 * 
 * @return Progress from 0.0 (start) to 1.0 (complete)
 */
float auto_dj_get_crossfade_progress(void);

/**
 * @brief Check if crossfade is in progress
 * 
 * @return true if crossfading
 */
bool auto_dj_is_crossfading(void);

/**
 * @brief Calculate fade gain for outgoing deck
 * 
 * @param progress Crossfade progress (0.0 to 1.0)
 * @param curve Crossfade curve type
 * @return Gain multiplier (0.0 to 1.0)
 */
float auto_dj_calc_fade_out_gain(float progress, auto_dj_curve_t curve);

/**
 * @brief Calculate fade gain for incoming deck
 * 
 * @param progress Crossfade progress (0.0 to 1.0)
 * @param curve Crossfade curve type
 * @return Gain multiplier (0.0 to 1.0)
 */
float auto_dj_calc_fade_in_gain(float progress, auto_dj_curve_t curve);

// ============================================================================
// Status and Diagnostics
// ============================================================================

/**
 * @brief Get currently playing track info
 * 
 * @param entry Pointer to store current track info
 * @return true if a track is playing
 */
bool auto_dj_get_current_track(auto_dj_queue_entry_t *entry);

/**
 * @brief Get next track info (during crossfade)
 * 
 * @param entry Pointer to store next track info
 * @return true if next track is loaded
 */
bool auto_dj_get_next_track(auto_dj_queue_entry_t *entry);

/**
 * @brief Get time until next crossfade starts
 * 
 * @return Milliseconds until crossfade, or 0 if not applicable
 */
uint32_t auto_dj_get_time_to_crossfade(void);

/**
 * @brief Get state name as string
 * 
 * @param state State enum value
 * @return Human-readable state name
 */
const char* auto_dj_state_name(auto_dj_state_t state);

/**
 * @brief Get curve name as string
 * 
 * @param curve Curve enum value
 * @return Human-readable curve name
 */
const char* auto_dj_curve_name(auto_dj_curve_t curve);

#ifdef __cplusplus
}
#endif

#endif /* AUTO_DJ_H */
