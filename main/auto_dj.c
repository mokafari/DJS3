/**
 * @file auto_dj.c
 * @brief Auto-DJ system implementation
 * 
 * Manages automated playback with intelligent mixing, queue management,
 * and BPM/key-based track selection.
 */

#include "auto_dj.h"
#include "audio_player.h"
#include "metadata.h"
#include "metadata_format.h"
#include "track_history.h"
#include "library_db.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

static const char *TAG = "auto_dj";

// ============================================================================
// Internal State
// ============================================================================

// Queue storage (circular buffer)
static auto_dj_queue_entry_t s_queue[AUTO_DJ_QUEUE_MAX];
static uint32_t s_queue_head = 0;   // Next position to read
static uint32_t s_queue_tail = 0;   // Next position to write
static uint32_t s_queue_count = 0;

// Current configuration
static auto_dj_config_t s_config;

// State machine
static auto_dj_state_t s_state = AUTO_DJ_STATE_DISABLED;
static bool s_paused = false;

// Crossfade state
static uint32_t s_crossfade_start_time = 0;
static float s_crossfade_progress = 0.0f;

// Currently playing track info
static auto_dj_queue_entry_t s_current_track;
static auto_dj_queue_entry_t s_next_track;
static bool s_has_current = false;
static bool s_has_next = false;

// Event callback
static auto_dj_event_cb_t s_event_callback = NULL;

// Thread safety
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_time_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief Lock mutex with timeout
 */
static bool lock_mutex(void) {
    if (!s_mutex) return false;
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

/**
 * @brief Unlock mutex
 */
static void unlock_mutex(void) {
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

/**
 * @brief Send event to callback
 */
static void send_event(auto_dj_event_t event, const char *data) {
    if (s_event_callback) {
        s_event_callback(event, data);
    }
}

/**
 * @brief Extract filename from path for display
 */
static void extract_title(const char *filepath, char *title, size_t max_len) {
    const char *slash = strrchr(filepath, '/');
    const char *filename = slash ? slash + 1 : filepath;
    
    // Copy without extension
    strncpy(title, filename, max_len - 1);
    title[max_len - 1] = '\0';
    
    char *dot = strrchr(title, '.');
    if (dot) *dot = '\0';
}

/**
 * @brief Load metadata for a queue entry
 */
static void load_entry_metadata(auto_dj_queue_entry_t *entry) {
    TrackMetadata_t meta;
    
    if (metadata_load(entry->filepath, &meta)) {
        entry->bpm = meta.bpm;
        entry->key_id = meta.key_id;
        entry->duration_ms = meta.duration_ms;
        entry->analyzed = true;
    } else {
        entry->bpm = 0.0f;
        entry->key_id = 255;
        entry->duration_ms = 0;
        entry->analyzed = false;
    }
    
    extract_title(entry->filepath, entry->title, sizeof(entry->title));
}

/**
 * @brief Get queue index from position
 */
static uint32_t queue_index(uint32_t position) {
    return (s_queue_head + position) % AUTO_DJ_QUEUE_MAX;
}

// ============================================================================
// Initialization
// ============================================================================

bool auto_dj_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Auto-DJ already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing Auto-DJ system");
    
    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Initialize state
    s_queue_head = 0;
    s_queue_tail = 0;
    s_queue_count = 0;
    s_state = AUTO_DJ_STATE_DISABLED;
    s_paused = false;
    s_has_current = false;
    s_has_next = false;
    s_event_callback = NULL;
    
    // Load default config
    auto_dj_get_default_config(&s_config);
    
    s_initialized = true;
    ESP_LOGI(TAG, "Auto-DJ initialized");
    return true;
}

void auto_dj_deinit(void) {
    if (!s_initialized) return;
    
    ESP_LOGI(TAG, "Deinitializing Auto-DJ");
    
    auto_dj_disable();
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
}

void auto_dj_set_event_callback(auto_dj_event_cb_t callback) {
    s_event_callback = callback;
}

// ============================================================================
// Configuration
// ============================================================================

void auto_dj_get_config(auto_dj_config_t *config) {
    if (!config) return;
    
    if (lock_mutex()) {
        *config = s_config;
        unlock_mutex();
    }
}

bool auto_dj_set_config(const auto_dj_config_t *config) {
    if (!config) return false;
    
    if (!lock_mutex()) return false;
    
    // Validate crossfade time
    auto_dj_config_t new_config = *config;
    if (new_config.crossfade_ms < AUTO_DJ_MIN_CROSSFADE_MS) {
        new_config.crossfade_ms = AUTO_DJ_MIN_CROSSFADE_MS;
    } else if (new_config.crossfade_ms > AUTO_DJ_MAX_CROSSFADE_MS) {
        new_config.crossfade_ms = AUTO_DJ_MAX_CROSSFADE_MS;
    }
    
    // Validate max BPM adjust
    if (new_config.max_bpm_adjust > AUTO_DJ_MAX_BPM_ADJUST_PERCENT) {
        new_config.max_bpm_adjust = AUTO_DJ_MAX_BPM_ADJUST_PERCENT;
    }
    
    s_config = new_config;
    
    unlock_mutex();
    
    ESP_LOGI(TAG, "Config updated: crossfade=%"PRIu32"ms, curve=%d, mode=%d",
             s_config.crossfade_ms, s_config.curve, s_config.select_mode);
    
    return true;
}

void auto_dj_get_default_config(auto_dj_config_t *config) {
    if (!config) return;
    
    config->crossfade_ms = AUTO_DJ_DEFAULT_CROSSFADE_MS;
    config->curve = AUTO_DJ_CURVE_EQUAL_POWER;
    config->select_mode = AUTO_DJ_SELECT_SEQUENTIAL;
    config->bpm_sync_enabled = true;
    config->key_lock_enabled = true;
    config->max_bpm_adjust = AUTO_DJ_MAX_BPM_ADJUST_PERCENT;
    config->avoid_recent = true;
    config->recent_hours = 4;
}

bool auto_dj_set_crossfade_time(uint32_t ms) {
    if (ms < AUTO_DJ_MIN_CROSSFADE_MS) ms = AUTO_DJ_MIN_CROSSFADE_MS;
    if (ms > AUTO_DJ_MAX_CROSSFADE_MS) ms = AUTO_DJ_MAX_CROSSFADE_MS;
    
    if (!lock_mutex()) return false;
    s_config.crossfade_ms = ms;
    unlock_mutex();
    
    return true;
}

uint32_t auto_dj_get_crossfade_time(void) {
    uint32_t ms = AUTO_DJ_DEFAULT_CROSSFADE_MS;
    if (lock_mutex()) {
        ms = s_config.crossfade_ms;
        unlock_mutex();
    }
    return ms;
}

void auto_dj_set_curve(auto_dj_curve_t curve) {
    if (lock_mutex()) {
        s_config.curve = curve;
        unlock_mutex();
    }
}

void auto_dj_set_select_mode(auto_dj_select_mode_t mode) {
    if (lock_mutex()) {
        s_config.select_mode = mode;
        unlock_mutex();
    }
}

// ============================================================================
// Control
// ============================================================================

bool auto_dj_enable(void) {
    if (!s_initialized) return false;
    
    if (!lock_mutex()) return false;
    
    if (s_state != AUTO_DJ_STATE_DISABLED) {
        unlock_mutex();
        return true;  // Already enabled
    }
    
    ESP_LOGI(TAG, "Enabling Auto-DJ");
    
    if (s_queue_count == 0) {
        s_state = AUTO_DJ_STATE_IDLE;
        ESP_LOGI(TAG, "Auto-DJ enabled (queue empty, waiting for tracks)");
    } else {
        // Start playback of first track
        auto_dj_queue_entry_t entry = s_queue[s_queue_head];
        
        // Pop from queue
        s_queue_head = (s_queue_head + 1) % AUTO_DJ_QUEUE_MAX;
        s_queue_count--;
        
        s_current_track = entry;
        s_has_current = true;
        s_state = AUTO_DJ_STATE_PLAYING;
        
        // Actually load and play the track
        if (audio_player_load(entry.filepath)) {
            audio_player_play();
            track_history_record_play(entry.filepath);
            send_event(AUTO_DJ_EVENT_TRACK_LOADED, entry.filepath);
        } else {
            ESP_LOGW(TAG, "Failed to load first track: %s", entry.filepath);
            s_state = AUTO_DJ_STATE_IDLE;
            s_has_current = false;
        }
    }
    
    s_paused = false;
    
    unlock_mutex();
    
    send_event(AUTO_DJ_EVENT_STARTED, NULL);
    return true;
}

void auto_dj_disable(void) {
    if (!s_initialized) return;
    
    if (!lock_mutex()) return;
    
    if (s_state == AUTO_DJ_STATE_DISABLED) {
        unlock_mutex();
        return;
    }
    
    ESP_LOGI(TAG, "Disabling Auto-DJ");
    
    s_state = AUTO_DJ_STATE_DISABLED;
    s_paused = false;
    s_has_next = false;
    s_crossfade_progress = 0.0f;
    
    // Keep current track playing, but clear next track reference
    
    unlock_mutex();
    
    send_event(AUTO_DJ_EVENT_STOPPED, NULL);
}

bool auto_dj_is_enabled(void) {
    return s_state != AUTO_DJ_STATE_DISABLED;
}

auto_dj_state_t auto_dj_get_state(void) {
    return s_state;
}

void auto_dj_pause(void) {
    if (!lock_mutex()) return;
    s_paused = true;
    if (s_state == AUTO_DJ_STATE_PLAYING) {
        s_state = AUTO_DJ_STATE_PAUSED;
    }
    unlock_mutex();
}

void auto_dj_resume(void) {
    if (!lock_mutex()) return;
    s_paused = false;
    if (s_state == AUTO_DJ_STATE_PAUSED) {
        s_state = AUTO_DJ_STATE_PLAYING;
    }
    unlock_mutex();
}

bool auto_dj_skip(void) {
    if (!s_initialized) return false;
    if (s_state == AUTO_DJ_STATE_DISABLED) return false;
    
    if (!lock_mutex()) return false;
    
    if (s_queue_count == 0) {
        unlock_mutex();
        ESP_LOGW(TAG, "Cannot skip - queue empty");
        return false;
    }
    
    // Start crossfade immediately
    s_crossfade_start_time = get_time_ms();
    s_crossfade_progress = 0.0f;
    s_state = AUTO_DJ_STATE_CROSSFADING;
    
    // Load next track
    auto_dj_queue_entry_t entry = s_queue[s_queue_head];
    s_queue_head = (s_queue_head + 1) % AUTO_DJ_QUEUE_MAX;
    s_queue_count--;
    
    s_next_track = entry;
    s_has_next = true;
    
    unlock_mutex();
    
    ESP_LOGI(TAG, "Skip triggered - crossfading to: %s", entry.title);
    send_event(AUTO_DJ_EVENT_CROSSFADE_START, entry.filepath);
    
    return true;
}

void auto_dj_update(void) {
    if (!s_initialized) return;
    if (s_state == AUTO_DJ_STATE_DISABLED || s_paused) return;
    
    if (!lock_mutex()) return;
    
    auto_dj_state_t current_state = s_state;
    uint32_t now = get_time_ms();
    
    switch (current_state) {
        case AUTO_DJ_STATE_IDLE:
            // Check if queue has tracks
            if (s_queue_count > 0) {
                auto_dj_queue_entry_t entry = s_queue[s_queue_head];
                s_queue_head = (s_queue_head + 1) % AUTO_DJ_QUEUE_MAX;
                s_queue_count--;
                
                s_current_track = entry;
                s_has_current = true;
                
                if (audio_player_load(entry.filepath)) {
                    audio_player_play();
                    track_history_record_play(entry.filepath);
                    s_state = AUTO_DJ_STATE_PLAYING;
                    
                    unlock_mutex();
                    send_event(AUTO_DJ_EVENT_TRACK_LOADED, entry.filepath);
                    return;
                }
            }
            break;
            
        case AUTO_DJ_STATE_PLAYING: {
            // Check if we should start crossfade
            uint32_t position_ms = audio_player_get_position_ms();
            uint32_t duration_ms = audio_player_get_duration_ms();
            
            if (duration_ms > 0 && s_queue_count > 0) {
                uint32_t crossfade_start = duration_ms - s_config.crossfade_ms;
                
                if (position_ms >= crossfade_start) {
                    // Time to start crossfade
                    s_crossfade_start_time = now;
                    s_crossfade_progress = 0.0f;
                    s_state = AUTO_DJ_STATE_CROSSFADING;
                    
                    // Load next track
                    auto_dj_queue_entry_t entry = s_queue[s_queue_head];
                    s_queue_head = (s_queue_head + 1) % AUTO_DJ_QUEUE_MAX;
                    s_queue_count--;
                    
                    s_next_track = entry;
                    s_has_next = true;
                    
                    // Preload next track (audio player should support this)
                    // For now, we'll handle the transition in CROSSFADING state
                    
                    unlock_mutex();
                    send_event(AUTO_DJ_EVENT_CROSSFADE_START, entry.filepath);
                    
                    // Check for low queue
                    if (s_queue_count < 5) {
                        send_event(AUTO_DJ_EVENT_QUEUE_LOW, NULL);
                    }
                    return;
                }
            } else if (duration_ms > 0 && position_ms >= duration_ms - 100) {
                // Track ending but queue empty
                s_state = AUTO_DJ_STATE_IDLE;
                s_has_current = false;
                
                unlock_mutex();
                send_event(AUTO_DJ_EVENT_QUEUE_EMPTY, NULL);
                return;
            }
            break;
        }
            
        case AUTO_DJ_STATE_CROSSFADING: {
            // Update crossfade progress
            uint32_t elapsed = now - s_crossfade_start_time;
            s_crossfade_progress = (float)elapsed / (float)s_config.crossfade_ms;
            
            if (s_crossfade_progress >= 1.0f) {
                s_crossfade_progress = 1.0f;
                
                // Crossfade complete - switch to next track
                s_current_track = s_next_track;
                s_has_next = false;
                s_state = AUTO_DJ_STATE_PLAYING;
                
                // Load and start the new track
                if (audio_player_load(s_current_track.filepath)) {
                    audio_player_play();
                    track_history_record_play(s_current_track.filepath);
                }
                
                unlock_mutex();
                send_event(AUTO_DJ_EVENT_CROSSFADE_END, s_current_track.filepath);
                send_event(AUTO_DJ_EVENT_TRACK_LOADED, s_current_track.filepath);
                return;
            }
            
            // Apply crossfade gains
            float out_gain = auto_dj_calc_fade_out_gain(s_crossfade_progress, s_config.curve);
            float in_gain = auto_dj_calc_fade_in_gain(s_crossfade_progress, s_config.curve);
            
            // Set gains on audio player (this is a simplified model)
            // In a real dual-deck system, you'd control two separate players
            audio_player_set_gain(out_gain);
            
            break;
        }
            
        default:
            break;
    }
    
    unlock_mutex();
}

// ============================================================================
// Queue Management
// ============================================================================

bool auto_dj_queue_add(const char *filepath) {
    if (!s_initialized || !filepath) return false;
    
    if (!lock_mutex()) return false;
    
    if (s_queue_count >= AUTO_DJ_QUEUE_MAX) {
        unlock_mutex();
        ESP_LOGW(TAG, "Queue full, cannot add: %s", filepath);
        return false;
    }
    
    // Create entry
    auto_dj_queue_entry_t *entry = &s_queue[s_queue_tail];
    strncpy(entry->filepath, filepath, AUTO_DJ_PATH_MAX - 1);
    entry->filepath[AUTO_DJ_PATH_MAX - 1] = '\0';
    
    // Load metadata
    load_entry_metadata(entry);
    
    // Advance tail
    s_queue_tail = (s_queue_tail + 1) % AUTO_DJ_QUEUE_MAX;
    s_queue_count++;
    
    unlock_mutex();
    
    ESP_LOGD(TAG, "Added to queue: %s (BPM: %.1f, Key: %s)",
             entry->title, entry->bpm, 
             entry->key_id < 24 ? CAMELOT_KEYS[entry->key_id] : "?");
    
    return true;
}

bool auto_dj_queue_insert(const char *filepath, uint32_t position) {
    if (!s_initialized || !filepath) return false;
    
    if (!lock_mutex()) return false;
    
    if (s_queue_count >= AUTO_DJ_QUEUE_MAX) {
        unlock_mutex();
        return false;
    }
    
    if (position > s_queue_count) {
        position = s_queue_count;
    }
    
    // Create new entry
    auto_dj_queue_entry_t new_entry;
    strncpy(new_entry.filepath, filepath, AUTO_DJ_PATH_MAX - 1);
    new_entry.filepath[AUTO_DJ_PATH_MAX - 1] = '\0';
    load_entry_metadata(&new_entry);
    
    // Shift entries to make room
    for (uint32_t i = s_queue_count; i > position; i--) {
        uint32_t dst = queue_index(i);
        uint32_t src = queue_index(i - 1);
        s_queue[dst] = s_queue[src];
    }
    
    // Insert new entry
    s_queue[queue_index(position)] = new_entry;
    s_queue_count++;
    s_queue_tail = (s_queue_tail + 1) % AUTO_DJ_QUEUE_MAX;
    
    unlock_mutex();
    
    ESP_LOGD(TAG, "Inserted at position %"PRIu32": %s", position, new_entry.title);
    return true;
}

uint32_t auto_dj_queue_add_multiple(const char **filepaths, uint32_t count) {
    if (!s_initialized || !filepaths) return 0;
    
    uint32_t added = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (auto_dj_queue_add(filepaths[i])) {
            added++;
        }
    }
    return added;
}

bool auto_dj_queue_remove(uint32_t position) {
    if (!s_initialized) return false;
    
    if (!lock_mutex()) return false;
    
    if (position >= s_queue_count) {
        unlock_mutex();
        return false;
    }
    
    // Shift entries down
    for (uint32_t i = position; i < s_queue_count - 1; i++) {
        uint32_t dst = queue_index(i);
        uint32_t src = queue_index(i + 1);
        s_queue[dst] = s_queue[src];
    }
    
    s_queue_count--;
    s_queue_tail = (s_queue_head + s_queue_count) % AUTO_DJ_QUEUE_MAX;
    
    unlock_mutex();
    return true;
}

bool auto_dj_queue_remove_path(const char *filepath) {
    if (!s_initialized || !filepath) return false;
    
    if (!lock_mutex()) return false;
    
    for (uint32_t i = 0; i < s_queue_count; i++) {
        uint32_t idx = queue_index(i);
        if (strcmp(s_queue[idx].filepath, filepath) == 0) {
            unlock_mutex();
            return auto_dj_queue_remove(i);
        }
    }
    
    unlock_mutex();
    return false;
}

bool auto_dj_queue_move(uint32_t from_pos, uint32_t to_pos) {
    if (!s_initialized) return false;
    
    if (!lock_mutex()) return false;
    
    if (from_pos >= s_queue_count || to_pos >= s_queue_count) {
        unlock_mutex();
        return false;
    }
    
    if (from_pos == to_pos) {
        unlock_mutex();
        return true;
    }
    
    // Save the entry being moved
    auto_dj_queue_entry_t moving = s_queue[queue_index(from_pos)];
    
    // Shift entries
    if (from_pos < to_pos) {
        for (uint32_t i = from_pos; i < to_pos; i++) {
            s_queue[queue_index(i)] = s_queue[queue_index(i + 1)];
        }
    } else {
        for (uint32_t i = from_pos; i > to_pos; i--) {
            s_queue[queue_index(i)] = s_queue[queue_index(i - 1)];
        }
    }
    
    // Place moved entry
    s_queue[queue_index(to_pos)] = moving;
    
    unlock_mutex();
    return true;
}

void auto_dj_queue_clear(void) {
    if (!s_initialized) return;
    
    if (!lock_mutex()) return;
    
    s_queue_head = 0;
    s_queue_tail = 0;
    s_queue_count = 0;
    
    unlock_mutex();
    
    ESP_LOGI(TAG, "Queue cleared");
}

void auto_dj_queue_shuffle(void) {
    if (!s_initialized) return;
    
    if (!lock_mutex()) return;
    
    if (s_queue_count < 2) {
        unlock_mutex();
        return;
    }
    
    // Fisher-Yates shuffle
    srand((unsigned int)time(NULL));
    
    for (uint32_t i = s_queue_count - 1; i > 0; i--) {
        uint32_t j = rand() % (i + 1);
        
        // Swap entries
        uint32_t idx_i = queue_index(i);
        uint32_t idx_j = queue_index(j);
        
        auto_dj_queue_entry_t temp = s_queue[idx_i];
        s_queue[idx_i] = s_queue[idx_j];
        s_queue[idx_j] = temp;
    }
    
    unlock_mutex();
    
    ESP_LOGI(TAG, "Queue shuffled");
}

uint32_t auto_dj_queue_count(void) {
    return s_queue_count;
}

bool auto_dj_queue_get(uint32_t position, auto_dj_queue_entry_t *entry) {
    if (!s_initialized || !entry) return false;
    
    if (!lock_mutex()) return false;
    
    if (position >= s_queue_count) {
        unlock_mutex();
        return false;
    }
    
    *entry = s_queue[queue_index(position)];
    
    unlock_mutex();
    return true;
}

bool auto_dj_queue_peek_next(auto_dj_queue_entry_t *entry) {
    return auto_dj_queue_get(0, entry);
}

bool auto_dj_queue_contains(const char *filepath) {
    if (!s_initialized || !filepath) return false;
    
    if (!lock_mutex()) return false;
    
    for (uint32_t i = 0; i < s_queue_count; i++) {
        if (strcmp(s_queue[queue_index(i)].filepath, filepath) == 0) {
            unlock_mutex();
            return true;
        }
    }
    
    unlock_mutex();
    return false;
}

// ============================================================================
// BPM Matching
// ============================================================================

bool auto_dj_bpm_compatible(float bpm1, float bpm2) {
    if (bpm1 <= 0 || bpm2 <= 0) return true;  // Unknown BPM = compatible
    
    // Normalize BPMs
    float norm1 = auto_dj_normalize_bpm(bpm1);
    float norm2 = auto_dj_normalize_bpm(bpm2);
    
    float diff = fabsf(norm1 - norm2);
    float avg = (norm1 + norm2) / 2.0f;
    float percent_diff = (diff / avg) * 100.0f;
    
    return percent_diff <= AUTO_DJ_BPM_TOLERANCE_PERCENT;
}

float auto_dj_bpm_diff_percent(float from_bpm, float to_bpm) {
    if (from_bpm <= 0 || to_bpm <= 0) return 0.0f;
    
    return ((to_bpm - from_bpm) / from_bpm) * 100.0f;
}

float auto_dj_calc_pitch_adjust(float from_bpm, float to_bpm) {
    if (from_bpm <= 0 || to_bpm <= 0) return 1.0f;
    
    // Normalize to typical DJ range
    float norm_from = auto_dj_normalize_bpm(from_bpm);
    float norm_to = auto_dj_normalize_bpm(to_bpm);
    
    float ratio = norm_to / norm_from;
    
    // Clamp to max adjustment
    float max_ratio = 1.0f + (AUTO_DJ_MAX_BPM_ADJUST_PERCENT / 100.0f);
    float min_ratio = 1.0f - (AUTO_DJ_MAX_BPM_ADJUST_PERCENT / 100.0f);
    
    if (ratio > max_ratio) ratio = max_ratio;
    if (ratio < min_ratio) ratio = min_ratio;
    
    return ratio;
}

float auto_dj_normalize_bpm(float bpm) {
    if (bpm <= 0) return 0;
    
    // Normalize to 80-160 range
    while (bpm < 80.0f) bpm *= 2.0f;
    while (bpm > 160.0f) bpm /= 2.0f;
    
    return bpm;
}

// ============================================================================
// Key Compatibility (Camelot Wheel)
// ============================================================================

key_compat_t auto_dj_key_compatibility(uint8_t key1, uint8_t key2) {
    if (key1 > 23 || key2 > 23) return KEY_COMPAT_NONE;
    
    // Same key
    if (key1 == key2) return KEY_COMPAT_SAME;
    
    // Extract number (1-12) and mode (A=minor, B=major)
    uint8_t num1 = (key1 % 12) + 1;
    uint8_t num2 = (key2 % 12) + 1;
    bool minor1 = key1 < 12;
    bool minor2 = key2 < 12;
    
    // Same number, different mode = relative major/minor
    if (num1 == num2 && minor1 != minor2) {
        return KEY_COMPAT_RELATIVE;
    }
    
    // Same mode
    if (minor1 == minor2) {
        // Adjacent on wheel (+/- 1)
        int diff = (int)num2 - (int)num1;
        if (diff < 0) diff += 12;
        
        if (diff == 1 || diff == 11) {
            return KEY_COMPAT_ADJACENT;
        }
        
        // Energy boost (+7 = perfect 5th up)
        if (diff == 7) {
            return KEY_COMPAT_ENERGY_BOOST;
        }
        
        // Energy drop (-7 = 5 = perfect 5th down)
        if (diff == 5) {
            return KEY_COMPAT_ENERGY_DROP;
        }
    }
    
    return KEY_COMPAT_NONE;
}

uint32_t auto_dj_get_compatible_keys(uint8_t key_id, uint8_t *compat_keys, uint32_t max_keys) {
    if (key_id > 23 || !compat_keys || max_keys == 0) return 0;
    
    uint32_t count = 0;
    
    // Extract components
    uint8_t num = key_id % 12;      // 0-11
    bool is_minor = key_id < 12;
    
    // Same key
    if (count < max_keys) compat_keys[count++] = key_id;
    
    // Relative major/minor (same number, opposite mode)
    uint8_t relative = is_minor ? (num + 12) : num;
    if (count < max_keys) compat_keys[count++] = relative;
    
    // Adjacent +1
    uint8_t next = is_minor ? ((num + 1) % 12) : (((num + 1) % 12) + 12);
    if (count < max_keys) compat_keys[count++] = next;
    
    // Adjacent -1
    uint8_t prev = is_minor ? ((num + 11) % 12) : (((num + 11) % 12) + 12);
    if (count < max_keys) compat_keys[count++] = prev;
    
    // Energy boost (+7)
    uint8_t boost = is_minor ? ((num + 7) % 12) : (((num + 7) % 12) + 12);
    if (count < max_keys) compat_keys[count++] = boost;
    
    // Energy drop (+5 = -7)
    uint8_t drop = is_minor ? ((num + 5) % 12) : (((num + 5) % 12) + 12);
    if (count < max_keys) compat_keys[count++] = drop;
    
    return count;
}

const char* auto_dj_get_key_name(uint8_t key_id) {
    if (key_id > 23) return "?";
    return CAMELOT_KEYS[key_id];
}

// ============================================================================
// Track Suggestions
// ============================================================================

uint32_t auto_dj_get_suggestions(auto_dj_suggestion_t *suggestions, uint32_t max_count,
                                   float current_bpm, uint8_t current_key) {
    if (!suggestions || max_count == 0) return 0;
    
    // This would integrate with library_db to scan all tracks
    // For now, return 0 as we need the library DB integration
    
    ESP_LOGD(TAG, "Suggestion request: BPM=%.1f, Key=%s", 
             current_bpm, current_key < 24 ? CAMELOT_KEYS[current_key] : "?");
    
    // TODO: Implement full library scan with compatibility scoring
    // This requires integration with library_db.h
    
    return 0;
}

uint32_t auto_dj_calc_compatibility(float bpm1, uint8_t key1, float bpm2, uint8_t key2) {
    uint32_t score = 0;
    
    // BPM component (0-500 points)
    if (bpm1 > 0 && bpm2 > 0) {
        float bpm_diff = fabsf(auto_dj_bpm_diff_percent(bpm1, bpm2));
        if (bpm_diff <= 1.0f) {
            score += 500;
        } else if (bpm_diff <= 3.0f) {
            score += 400;
        } else if (bpm_diff <= 6.0f) {
            score += 300;
        } else if (bpm_diff <= 10.0f) {
            score += 100;
        }
    } else {
        score += 250;  // Unknown BPM = neutral
    }
    
    // Key component (0-500 points)
    if (key1 <= 23 && key2 <= 23) {
        key_compat_t compat = auto_dj_key_compatibility(key1, key2);
        switch (compat) {
            case KEY_COMPAT_SAME:         score += 500; break;
            case KEY_COMPAT_ADJACENT:     score += 450; break;
            case KEY_COMPAT_RELATIVE:     score += 400; break;
            case KEY_COMPAT_ENERGY_BOOST: score += 350; break;
            case KEY_COMPAT_ENERGY_DROP:  score += 350; break;
            default:                      score += 0;   break;
        }
    } else {
        score += 250;  // Unknown key = neutral
    }
    
    return score;
}

// ============================================================================
// Crossfade Control
// ============================================================================

float auto_dj_get_crossfade_progress(void) {
    return s_crossfade_progress;
}

bool auto_dj_is_crossfading(void) {
    return s_state == AUTO_DJ_STATE_CROSSFADING;
}

float auto_dj_calc_fade_out_gain(float progress, auto_dj_curve_t curve) {
    if (progress <= 0.0f) return 1.0f;
    if (progress >= 1.0f) return 0.0f;
    
    switch (curve) {
        case AUTO_DJ_CURVE_LINEAR:
            return 1.0f - progress;
            
        case AUTO_DJ_CURVE_EQUAL_POWER:
            return cosf(progress * M_PI / 2.0f);
            
        case AUTO_DJ_CURVE_SLOW_CUT:
            // Slow fade out (exponential decay)
            return powf(1.0f - progress, 2.0f);
            
        case AUTO_DJ_CURVE_FAST_CUT:
            // Fast fade out (square root)
            return 1.0f - sqrtf(progress);
            
        case AUTO_DJ_CURVE_HARD_CUT:
            return progress < 0.5f ? 1.0f : 0.0f;
            
        default:
            return 1.0f - progress;
    }
}

float auto_dj_calc_fade_in_gain(float progress, auto_dj_curve_t curve) {
    if (progress <= 0.0f) return 0.0f;
    if (progress >= 1.0f) return 1.0f;
    
    switch (curve) {
        case AUTO_DJ_CURVE_LINEAR:
            return progress;
            
        case AUTO_DJ_CURVE_EQUAL_POWER:
            return sinf(progress * M_PI / 2.0f);
            
        case AUTO_DJ_CURVE_SLOW_CUT:
            // Quick fade in (complementary to slow out)
            return sqrtf(progress);
            
        case AUTO_DJ_CURVE_FAST_CUT:
            // Slow fade in
            return powf(progress, 2.0f);
            
        case AUTO_DJ_CURVE_HARD_CUT:
            return progress >= 0.5f ? 1.0f : 0.0f;
            
        default:
            return progress;
    }
}

// ============================================================================
// Status and Diagnostics
// ============================================================================

bool auto_dj_get_current_track(auto_dj_queue_entry_t *entry) {
    if (!entry) return false;
    
    if (!lock_mutex()) return false;
    
    if (s_has_current) {
        *entry = s_current_track;
        unlock_mutex();
        return true;
    }
    
    unlock_mutex();
    return false;
}

bool auto_dj_get_next_track(auto_dj_queue_entry_t *entry) {
    if (!entry) return false;
    
    if (!lock_mutex()) return false;
    
    if (s_has_next) {
        *entry = s_next_track;
        unlock_mutex();
        return true;
    }
    
    unlock_mutex();
    return false;
}

uint32_t auto_dj_get_time_to_crossfade(void) {
    if (s_state != AUTO_DJ_STATE_PLAYING) return 0;
    if (!s_has_current) return 0;
    
    uint32_t position_ms = audio_player_get_position_ms();
    uint32_t duration_ms = audio_player_get_duration_ms();
    
    if (duration_ms == 0) return 0;
    
    uint32_t crossfade_start = duration_ms - s_config.crossfade_ms;
    
    if (position_ms >= crossfade_start) return 0;
    
    return crossfade_start - position_ms;
}

const char* auto_dj_state_name(auto_dj_state_t state) {
    switch (state) {
        case AUTO_DJ_STATE_DISABLED:    return "Disabled";
        case AUTO_DJ_STATE_IDLE:        return "Idle";
        case AUTO_DJ_STATE_PLAYING:     return "Playing";
        case AUTO_DJ_STATE_CROSSFADING: return "Crossfading";
        case AUTO_DJ_STATE_PAUSED:      return "Paused";
        default:                        return "Unknown";
    }
}

const char* auto_dj_curve_name(auto_dj_curve_t curve) {
    switch (curve) {
        case AUTO_DJ_CURVE_LINEAR:      return "Linear";
        case AUTO_DJ_CURVE_EQUAL_POWER: return "Equal Power";
        case AUTO_DJ_CURVE_SLOW_CUT:    return "Slow Cut";
        case AUTO_DJ_CURVE_FAST_CUT:    return "Fast Cut";
        case AUTO_DJ_CURVE_HARD_CUT:    return "Hard Cut";
        default:                        return "Unknown";
    }
}
