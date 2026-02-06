/**
 * @file preferences.h
 * @brief User preferences persistence using NVS (Non-Volatile Storage)
 * 
 * Provides save/load functionality for user settings across categories:
 * theme, audio, display, and controls. Features lazy loading and caching
 * for performance optimization.
 */

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Limits
 * ============================================================================ */

#define PREFS_MAX_KEY_LEN       15   ///< NVS key length limit
#define PREFS_MAX_STRING_LEN    128  ///< Maximum string value length
#define PREFS_MAX_BLOB_SIZE     512  ///< Maximum blob size in bytes

/* ============================================================================
 * Settings Categories
 * ============================================================================ */

/**
 * @brief Preference categories (NVS namespaces)
 */
typedef enum {
    PREFS_CAT_THEME = 0,    ///< UI theme settings (colors, styles)
    PREFS_CAT_AUDIO,        ///< Audio settings (volume, EQ, etc.)
    PREFS_CAT_DISPLAY,      ///< Display settings (brightness, layout)
    PREFS_CAT_CONTROLS,     ///< Control settings (sensitivity, mapping)
    PREFS_CAT_COUNT         ///< Number of categories
} prefs_category_t;

/* ============================================================================
 * Theme Settings Keys
 * ============================================================================ */

// UI Colors (stored as uint32_t RGB888 values)
#define PREFS_KEY_THEME_PRIMARY      "theme_prim"      ///< Primary accent color
#define PREFS_KEY_THEME_SECONDARY    "theme_sec"       ///< Secondary accent color
#define PREFS_KEY_THEME_BACKGROUND   "theme_bg"        ///< Background color
#define PREFS_KEY_THEME_WAVEFORM     "theme_wave"      ///< Waveform color
#define PREFS_KEY_THEME_STYLE        "theme_style"     ///< UI style index (0=modern, 1=classic)

// Default theme values
#define PREFS_DEFAULT_THEME_PRIMARY      0x00FF7F00  ///< Orange
#define PREFS_DEFAULT_THEME_SECONDARY    0x0000BFFF  ///< Deep sky blue
#define PREFS_DEFAULT_THEME_BACKGROUND   0x001A1A1A  ///< Dark gray
#define PREFS_DEFAULT_THEME_WAVEFORM     0x0000FF00  ///< Green
#define PREFS_DEFAULT_THEME_STYLE        0           ///< Modern style

/* ============================================================================
 * Audio Settings Keys
 * ============================================================================ */

#define PREFS_KEY_AUDIO_MASTER_VOL   "aud_master"     ///< Master volume (0-100)
#define PREFS_KEY_AUDIO_CUE_VOL      "aud_cue"        ///< Cue/headphone volume (0-100)
#define PREFS_KEY_AUDIO_EQ_LOW       "aud_eq_low"     ///< Low EQ (-12 to +12 dB * 10)
#define PREFS_KEY_AUDIO_EQ_MID       "aud_eq_mid"     ///< Mid EQ (-12 to +12 dB * 10)
#define PREFS_KEY_AUDIO_EQ_HIGH      "aud_eq_high"    ///< High EQ (-12 to +12 dB * 10)
#define PREFS_KEY_AUDIO_LIMITER      "aud_limiter"    ///< Limiter enabled (0/1)
#define PREFS_KEY_AUDIO_AUTOGAIN     "aud_autogain"   ///< Auto-gain enabled (0/1)

// Default audio values
#define PREFS_DEFAULT_AUDIO_MASTER_VOL   80
#define PREFS_DEFAULT_AUDIO_CUE_VOL      70
#define PREFS_DEFAULT_AUDIO_EQ_LOW       0   ///< 0 dB
#define PREFS_DEFAULT_AUDIO_EQ_MID       0   ///< 0 dB
#define PREFS_DEFAULT_AUDIO_EQ_HIGH      0   ///< 0 dB
#define PREFS_DEFAULT_AUDIO_LIMITER      1   ///< Enabled
#define PREFS_DEFAULT_AUDIO_AUTOGAIN     0   ///< Disabled

/* ============================================================================
 * Display Settings Keys
 * ============================================================================ */

#define PREFS_KEY_DISP_BRIGHTNESS    "disp_bright"    ///< Brightness (0-255)
#define PREFS_KEY_DISP_AUTO_DIM      "disp_autodim"   ///< Auto-dim timeout (seconds, 0=off)
#define PREFS_KEY_DISP_WAVEFORM_ZOOM "disp_wv_zoom"   ///< Waveform zoom level (1-4)
#define PREFS_KEY_DISP_SHOW_GRID     "disp_grid"      ///< Show beat grid (0/1)
#define PREFS_KEY_DISP_SHOW_KEY      "disp_key"       ///< Show musical key (0/1)
#define PREFS_KEY_DISP_ROTATION      "disp_rotation"  ///< Screen rotation (0, 90, 180, 270)

// Default display values
#define PREFS_DEFAULT_DISP_BRIGHTNESS    200
#define PREFS_DEFAULT_DISP_AUTO_DIM      0    ///< Disabled
#define PREFS_DEFAULT_DISP_WAVEFORM_ZOOM 2    ///< Default zoom
#define PREFS_DEFAULT_DISP_SHOW_GRID     1    ///< Show grid
#define PREFS_DEFAULT_DISP_SHOW_KEY      1    ///< Show key
#define PREFS_DEFAULT_DISP_ROTATION      0    ///< No rotation

/* ============================================================================
 * Controls Settings Keys
 * ============================================================================ */

#define PREFS_KEY_CTRL_JOG_SENS      "ctrl_jog_sns"   ///< Jog wheel sensitivity (1-10)
#define PREFS_KEY_CTRL_PITCH_RANGE   "ctrl_pitch_rng" ///< Pitch range (4, 8, 16, 50 percent)
#define PREFS_KEY_CTRL_SCRATCH_MODE  "ctrl_scratch"   ///< Scratch mode (0=vinyl, 1=cdj)
#define PREFS_KEY_CTRL_CUE_MODE      "ctrl_cue_mode"  ///< Cue mode (0=cdj, 1=vinyl)
#define PREFS_KEY_CTRL_QUANTIZE      "ctrl_quantize"  ///< Quantize enabled (0/1)
#define PREFS_KEY_CTRL_SYNC_MODE     "ctrl_sync"      ///< Sync mode (0=off, 1=tempo, 2=beat)

// Default controls values
#define PREFS_DEFAULT_CTRL_JOG_SENS      5    ///< Medium sensitivity
#define PREFS_DEFAULT_CTRL_PITCH_RANGE   8    ///< ±8%
#define PREFS_DEFAULT_CTRL_SCRATCH_MODE  0    ///< Vinyl mode
#define PREFS_DEFAULT_CTRL_CUE_MODE      0    ///< CDJ mode
#define PREFS_DEFAULT_CTRL_QUANTIZE      1    ///< Enabled
#define PREFS_DEFAULT_CTRL_SYNC_MODE     1    ///< Tempo sync

/* ============================================================================
 * Error Codes
 * ============================================================================ */

/**
 * @brief Preference operation result codes
 */
typedef enum {
    PREFS_OK = 0,               ///< Success
    PREFS_ERR_NOT_INIT,         ///< Module not initialized
    PREFS_ERR_NVS_FAIL,         ///< NVS operation failed
    PREFS_ERR_NOT_FOUND,        ///< Key not found (default returned)
    PREFS_ERR_INVALID_PARAM,    ///< Invalid parameter
    PREFS_ERR_SIZE_MISMATCH,    ///< Size mismatch for blob
    PREFS_ERR_CACHE_FULL        ///< Cache is full
} prefs_result_t;

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================ */

/**
 * @brief Initialize preferences module
 * 
 * Opens NVS handles for all categories and initializes the cache.
 * Must be called after nvs_flash_init().
 * 
 * @return true on success, false on failure
 */
bool prefs_init(void);

/**
 * @brief Deinitialize preferences module
 * 
 * Commits any pending changes and closes NVS handles.
 */
void prefs_deinit(void);

/**
 * @brief Check if preferences module is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool prefs_is_initialized(void);

/* ============================================================================
 * Integer Values (int32_t)
 * ============================================================================ */

/**
 * @brief Get integer preference value
 * 
 * @param category Settings category
 * @param key      Preference key (max 15 chars)
 * @param value    Pointer to receive value
 * @param def      Default value if key not found
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_get_int(prefs_category_t category, const char *key, 
                             int32_t *value, int32_t def);

/**
 * @brief Set integer preference value
 * 
 * @param category Settings category
 * @param key      Preference key (max 15 chars)
 * @param value    Value to store
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_set_int(prefs_category_t category, const char *key, 
                             int32_t value);

/* ============================================================================
 * Unsigned Integer Values (uint32_t)
 * ============================================================================ */

/**
 * @brief Get unsigned integer preference value
 * 
 * @param category Settings category
 * @param key      Preference key (max 15 chars)
 * @param value    Pointer to receive value
 * @param def      Default value if key not found
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_get_uint(prefs_category_t category, const char *key, 
                              uint32_t *value, uint32_t def);

/**
 * @brief Set unsigned integer preference value
 * 
 * @param category Settings category
 * @param key      Preference key (max 15 chars)
 * @param value    Value to store
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_set_uint(prefs_category_t category, const char *key, 
                              uint32_t value);

/* ============================================================================
 * String Values
 * ============================================================================ */

/**
 * @brief Get string preference value
 * 
 * @param category  Settings category
 * @param key       Preference key (max 15 chars)
 * @param buffer    Buffer to receive string (null-terminated)
 * @param buf_size  Buffer size in bytes
 * @param def       Default string if key not found (can be NULL)
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_get_string(prefs_category_t category, const char *key, 
                                char *buffer, size_t buf_size, const char *def);

/**
 * @brief Set string preference value
 * 
 * @param category Settings category
 * @param key      Preference key (max 15 chars)
 * @param value    Null-terminated string to store
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_set_string(prefs_category_t category, const char *key, 
                                const char *value);

/* ============================================================================
 * Blob Values (Binary Data)
 * ============================================================================ */

/**
 * @brief Get blob preference value
 * 
 * @param category  Settings category
 * @param key       Preference key (max 15 chars)
 * @param buffer    Buffer to receive data
 * @param buf_size  Buffer size in bytes (will be updated with actual size)
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_get_blob(prefs_category_t category, const char *key, 
                              void *buffer, size_t *buf_size);

/**
 * @brief Set blob preference value
 * 
 * @param category Settings category
 * @param key      Preference key (max 15 chars)
 * @param data     Data to store
 * @param size     Size of data in bytes
 * @return PREFS_OK on success, error code otherwise
 */
prefs_result_t prefs_set_blob(prefs_category_t category, const char *key, 
                              const void *data, size_t size);

/* ============================================================================
 * Cache Management
 * ============================================================================ */

/**
 * @brief Commit all pending changes to NVS
 * 
 * Forces write of all modified values to flash storage.
 * Normally changes are committed automatically.
 * 
 * @return true if all commits successful, false otherwise
 */
bool prefs_commit(void);

/**
 * @brief Clear the in-memory cache
 * 
 * Forces next reads to fetch from NVS storage.
 * Does not affect stored values.
 */
void prefs_cache_clear(void);

/**
 * @brief Pre-load category values into cache
 * 
 * Useful at startup to avoid NVS reads during playback.
 * 
 * @param category Category to preload
 * @return Number of values loaded
 */
int prefs_cache_preload(prefs_category_t category);

/* ============================================================================
 * Reset and Defaults
 * ============================================================================ */

/**
 * @brief Reset category to default values
 * 
 * Erases all stored preferences in the category.
 * 
 * @param category Category to reset
 * @return true on success, false on failure
 */
bool prefs_reset_category(prefs_category_t category);

/**
 * @brief Reset all preferences to defaults
 * 
 * Erases all stored preferences across all categories.
 * 
 * @return true on success, false on failure
 */
bool prefs_reset_all(void);

/**
 * @brief Check if a key exists in storage
 * 
 * @param category Settings category
 * @param key      Preference key
 * @return true if key exists, false otherwise
 */
bool prefs_key_exists(prefs_category_t category, const char *key);

/**
 * @brief Erase a specific key
 * 
 * @param category Settings category
 * @param key      Preference key to erase
 * @return true on success, false on failure
 */
bool prefs_erase_key(prefs_category_t category, const char *key);

/* ============================================================================
 * Convenience Getters (Using Defaults)
 * ============================================================================ */

/**
 * @brief Get master volume (0-100)
 */
int32_t prefs_get_master_volume(void);

/**
 * @brief Set master volume (0-100)
 */
void prefs_set_master_volume(int32_t volume);

/**
 * @brief Get display brightness (0-255)
 */
uint8_t prefs_get_brightness(void);

/**
 * @brief Set display brightness (0-255)
 */
void prefs_set_brightness(uint8_t brightness);

/**
 * @brief Get jog wheel sensitivity (1-10)
 */
int32_t prefs_get_jog_sensitivity(void);

/**
 * @brief Set jog wheel sensitivity (1-10)
 */
void prefs_set_jog_sensitivity(int32_t sensitivity);

/**
 * @brief Get pitch range (4, 8, 16, or 50 percent)
 */
int32_t prefs_get_pitch_range(void);

/**
 * @brief Set pitch range (4, 8, 16, or 50 percent)
 */
void prefs_set_pitch_range(int32_t range);

/**
 * @brief Get quantize enabled state
 */
bool prefs_get_quantize_enabled(void);

/**
 * @brief Set quantize enabled state
 */
void prefs_set_quantize_enabled(bool enabled);

/**
 * @brief Get waveform color (RGB888)
 */
uint32_t prefs_get_waveform_color(void);

/**
 * @brief Set waveform color (RGB888)
 */
void prefs_set_waveform_color(uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* PREFERENCES_H */
