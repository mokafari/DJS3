/**
 * @file preferences.c
 * @brief User preferences persistence using NVS (Non-Volatile Storage)
 * 
 * Implements lazy loading and caching for performance optimization.
 * Uses separate NVS namespaces for each settings category.
 */

#include "preferences.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "preferences";

/* ============================================================================
 * NVS Namespace Names (max 15 chars)
 * ============================================================================ */

static const char *NAMESPACE_NAMES[PREFS_CAT_COUNT] = {
    [PREFS_CAT_THEME]    = "prefs_theme",
    [PREFS_CAT_AUDIO]    = "prefs_audio",
    [PREFS_CAT_DISPLAY]  = "prefs_display",
    [PREFS_CAT_CONTROLS] = "prefs_ctrl"
};

/* ============================================================================
 * Cache Configuration
 * ============================================================================ */

#define CACHE_MAX_ENTRIES   32    ///< Maximum cached values per category
#define CACHE_KEY_LEN       16    ///< Key length including null terminator

/**
 * @brief Cache entry for a single preference value
 */
typedef struct {
    char key[CACHE_KEY_LEN];      ///< Key name
    int32_t value_int;            ///< Integer value
    bool is_valid;                ///< Entry is valid/in-use
    bool is_dirty;                ///< Value modified but not committed
} cache_entry_t;

/**
 * @brief Cache for a single category
 */
typedef struct {
    cache_entry_t entries[CACHE_MAX_ENTRIES];
    int count;
} category_cache_t;

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    nvs_handle_t handles[PREFS_CAT_COUNT];  ///< NVS handles per category
    category_cache_t cache[PREFS_CAT_COUNT]; ///< Per-category caches
    bool initialized;
    bool handles_open[PREFS_CAT_COUNT];
} prefs_state = {0};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Open NVS handle for a category if not already open
 */
static esp_err_t ensure_handle_open(prefs_category_t category)
{
    if (category >= PREFS_CAT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (prefs_state.handles_open[category]) {
        return ESP_OK;
    }
    
    esp_err_t err = nvs_open(NAMESPACE_NAMES[category], NVS_READWRITE, 
                             &prefs_state.handles[category]);
    if (err == ESP_OK) {
        prefs_state.handles_open[category] = true;
        ESP_LOGD(TAG, "Opened NVS namespace: %s", NAMESPACE_NAMES[category]);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", 
                 NAMESPACE_NAMES[category], esp_err_to_name(err));
    }
    
    return err;
}

/**
 * @brief Find cache entry by key
 * @return Pointer to entry or NULL if not found
 */
static cache_entry_t* cache_find(prefs_category_t category, const char *key)
{
    if (category >= PREFS_CAT_COUNT || !key) {
        return NULL;
    }
    
    category_cache_t *cache = &prefs_state.cache[category];
    for (int i = 0; i < cache->count; i++) {
        if (cache->entries[i].is_valid && 
            strncmp(cache->entries[i].key, key, CACHE_KEY_LEN - 1) == 0) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Add or update cache entry
 * @return Pointer to entry or NULL if cache full
 */
static cache_entry_t* cache_set(prefs_category_t category, const char *key, 
                                int32_t value)
{
    if (category >= PREFS_CAT_COUNT || !key) {
        return NULL;
    }
    
    // Check if already exists
    cache_entry_t *entry = cache_find(category, key);
    if (entry) {
        entry->value_int = value;
        entry->is_dirty = true;
        return entry;
    }
    
    // Find empty slot
    category_cache_t *cache = &prefs_state.cache[category];
    if (cache->count >= CACHE_MAX_ENTRIES) {
        ESP_LOGW(TAG, "Cache full for category %d", category);
        return NULL;
    }
    
    entry = &cache->entries[cache->count];
    strncpy(entry->key, key, CACHE_KEY_LEN - 1);
    entry->key[CACHE_KEY_LEN - 1] = '\0';
    entry->value_int = value;
    entry->is_valid = true;
    entry->is_dirty = true;
    cache->count++;
    
    return entry;
}

/**
 * @brief Invalidate cache entry
 */
static void cache_invalidate(prefs_category_t category, const char *key)
{
    cache_entry_t *entry = cache_find(category, key);
    if (entry) {
        entry->is_valid = false;
    }
}

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================ */

bool prefs_init(void)
{
    if (prefs_state.initialized) {
        ESP_LOGW(TAG, "Preferences already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing preferences module");
    
    // Clear state
    memset(&prefs_state, 0, sizeof(prefs_state));
    
    // Open all NVS handles
    bool all_ok = true;
    for (int i = 0; i < PREFS_CAT_COUNT; i++) {
        if (ensure_handle_open(i) != ESP_OK) {
            all_ok = false;
        }
    }
    
    if (!all_ok) {
        ESP_LOGW(TAG, "Some NVS namespaces failed to open, creating on first use");
    }
    
    prefs_state.initialized = true;
    ESP_LOGI(TAG, "Preferences module initialized");
    
    return true;
}

void prefs_deinit(void)
{
    if (!prefs_state.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing preferences module");
    
    // Commit pending changes
    prefs_commit();
    
    // Close all NVS handles
    for (int i = 0; i < PREFS_CAT_COUNT; i++) {
        if (prefs_state.handles_open[i]) {
            nvs_close(prefs_state.handles[i]);
            prefs_state.handles_open[i] = false;
        }
    }
    
    prefs_state.initialized = false;
}

bool prefs_is_initialized(void)
{
    return prefs_state.initialized;
}

/* ============================================================================
 * Integer Values (int32_t)
 * ============================================================================ */

prefs_result_t prefs_get_int(prefs_category_t category, const char *key, 
                             int32_t *value, int32_t def)
{
    if (!prefs_state.initialized) {
        *value = def;
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key || !value) {
        if (value) *value = def;
        return PREFS_ERR_INVALID_PARAM;
    }
    
    // Check cache first (lazy loading)
    cache_entry_t *cached = cache_find(category, key);
    if (cached && cached->is_valid) {
        *value = cached->value_int;
        return PREFS_OK;
    }
    
    // Load from NVS
    if (ensure_handle_open(category) != ESP_OK) {
        *value = def;
        return PREFS_ERR_NVS_FAIL;
    }
    
    int32_t nvs_value;
    esp_err_t err = nvs_get_i32(prefs_state.handles[category], key, &nvs_value);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = def;
        // Cache the default value
        cache_entry_t *entry = cache_set(category, key, def);
        if (entry) entry->is_dirty = false;  // Don't write defaults
        return PREFS_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read failed for %s: %s", key, esp_err_to_name(err));
        *value = def;
        return PREFS_ERR_NVS_FAIL;
    }
    
    // Cache the loaded value
    cache_entry_t *entry = cache_set(category, key, nvs_value);
    if (entry) entry->is_dirty = false;  // Already in NVS
    
    *value = nvs_value;
    return PREFS_OK;
}

prefs_result_t prefs_set_int(prefs_category_t category, const char *key, 
                             int32_t value)
{
    if (!prefs_state.initialized) {
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key) {
        return PREFS_ERR_INVALID_PARAM;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return PREFS_ERR_NVS_FAIL;
    }
    
    // Update cache
    cache_entry_t *entry = cache_set(category, key, value);
    if (!entry) {
        return PREFS_ERR_CACHE_FULL;
    }
    
    // Write to NVS
    esp_err_t err = nvs_set_i32(prefs_state.handles[category], key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed for %s: %s", key, esp_err_to_name(err));
        return PREFS_ERR_NVS_FAIL;
    }
    
    entry->is_dirty = false;
    
    ESP_LOGD(TAG, "Set %s/%s = %ld", NAMESPACE_NAMES[category], key, (long)value);
    return PREFS_OK;
}

/* ============================================================================
 * Unsigned Integer Values (uint32_t)
 * ============================================================================ */

prefs_result_t prefs_get_uint(prefs_category_t category, const char *key, 
                              uint32_t *value, uint32_t def)
{
    if (!prefs_state.initialized) {
        *value = def;
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key || !value) {
        if (value) *value = def;
        return PREFS_ERR_INVALID_PARAM;
    }
    
    // Check cache first (store as int32_t internally)
    cache_entry_t *cached = cache_find(category, key);
    if (cached && cached->is_valid) {
        *value = (uint32_t)cached->value_int;
        return PREFS_OK;
    }
    
    // Load from NVS
    if (ensure_handle_open(category) != ESP_OK) {
        *value = def;
        return PREFS_ERR_NVS_FAIL;
    }
    
    uint32_t nvs_value;
    esp_err_t err = nvs_get_u32(prefs_state.handles[category], key, &nvs_value);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = def;
        // Cache the default value
        cache_entry_t *entry = cache_set(category, key, (int32_t)def);
        if (entry) entry->is_dirty = false;
        return PREFS_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read failed for %s: %s", key, esp_err_to_name(err));
        *value = def;
        return PREFS_ERR_NVS_FAIL;
    }
    
    // Cache the loaded value
    cache_entry_t *entry = cache_set(category, key, (int32_t)nvs_value);
    if (entry) entry->is_dirty = false;
    
    *value = nvs_value;
    return PREFS_OK;
}

prefs_result_t prefs_set_uint(prefs_category_t category, const char *key, 
                              uint32_t value)
{
    if (!prefs_state.initialized) {
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key) {
        return PREFS_ERR_INVALID_PARAM;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return PREFS_ERR_NVS_FAIL;
    }
    
    // Update cache
    cache_entry_t *entry = cache_set(category, key, (int32_t)value);
    if (!entry) {
        return PREFS_ERR_CACHE_FULL;
    }
    
    // Write to NVS
    esp_err_t err = nvs_set_u32(prefs_state.handles[category], key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed for %s: %s", key, esp_err_to_name(err));
        return PREFS_ERR_NVS_FAIL;
    }
    
    entry->is_dirty = false;
    
    ESP_LOGD(TAG, "Set %s/%s = %lu", NAMESPACE_NAMES[category], key, (unsigned long)value);
    return PREFS_OK;
}

/* ============================================================================
 * String Values
 * ============================================================================ */

prefs_result_t prefs_get_string(prefs_category_t category, const char *key, 
                                char *buffer, size_t buf_size, const char *def)
{
    if (!prefs_state.initialized) {
        if (buffer && def) {
            strncpy(buffer, def, buf_size - 1);
            buffer[buf_size - 1] = '\0';
        } else if (buffer) {
            buffer[0] = '\0';
        }
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key || !buffer || buf_size == 0) {
        return PREFS_ERR_INVALID_PARAM;
    }
    
    // Strings are not cached (could be large)
    if (ensure_handle_open(category) != ESP_OK) {
        if (def) {
            strncpy(buffer, def, buf_size - 1);
            buffer[buf_size - 1] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return PREFS_ERR_NVS_FAIL;
    }
    
    size_t required_size = 0;
    esp_err_t err = nvs_get_str(prefs_state.handles[category], key, NULL, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (def) {
            strncpy(buffer, def, buf_size - 1);
            buffer[buf_size - 1] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return PREFS_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read size failed for %s: %s", key, esp_err_to_name(err));
        if (def) {
            strncpy(buffer, def, buf_size - 1);
            buffer[buf_size - 1] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return PREFS_ERR_NVS_FAIL;
    }
    
    if (required_size > buf_size) {
        ESP_LOGW(TAG, "Buffer too small for %s: need %u, have %u", 
                 key, (unsigned)required_size, (unsigned)buf_size);
        // Read as much as we can
        required_size = buf_size;
    }
    
    err = nvs_get_str(prefs_state.handles[category], key, buffer, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read string failed for %s: %s", key, esp_err_to_name(err));
        if (def) {
            strncpy(buffer, def, buf_size - 1);
            buffer[buf_size - 1] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return PREFS_ERR_NVS_FAIL;
    }
    
    return PREFS_OK;
}

prefs_result_t prefs_set_string(prefs_category_t category, const char *key, 
                                const char *value)
{
    if (!prefs_state.initialized) {
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key || !value) {
        return PREFS_ERR_INVALID_PARAM;
    }
    
    if (strlen(value) > PREFS_MAX_STRING_LEN) {
        ESP_LOGW(TAG, "String too long for %s: %u bytes", key, (unsigned)strlen(value));
        return PREFS_ERR_SIZE_MISMATCH;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return PREFS_ERR_NVS_FAIL;
    }
    
    esp_err_t err = nvs_set_str(prefs_state.handles[category], key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write string failed for %s: %s", key, esp_err_to_name(err));
        return PREFS_ERR_NVS_FAIL;
    }
    
    ESP_LOGD(TAG, "Set %s/%s = \"%s\"", NAMESPACE_NAMES[category], key, value);
    return PREFS_OK;
}

/* ============================================================================
 * Blob Values (Binary Data)
 * ============================================================================ */

prefs_result_t prefs_get_blob(prefs_category_t category, const char *key, 
                              void *buffer, size_t *buf_size)
{
    if (!prefs_state.initialized) {
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key || !buffer || !buf_size || *buf_size == 0) {
        return PREFS_ERR_INVALID_PARAM;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return PREFS_ERR_NVS_FAIL;
    }
    
    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(prefs_state.handles[category], key, NULL, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return PREFS_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read blob size failed for %s: %s", key, esp_err_to_name(err));
        return PREFS_ERR_NVS_FAIL;
    }
    
    if (required_size > *buf_size) {
        ESP_LOGW(TAG, "Buffer too small for blob %s: need %u, have %u", 
                 key, (unsigned)required_size, (unsigned)*buf_size);
        *buf_size = required_size;  // Inform caller of required size
        return PREFS_ERR_SIZE_MISMATCH;
    }
    
    err = nvs_get_blob(prefs_state.handles[category], key, buffer, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read blob failed for %s: %s", key, esp_err_to_name(err));
        return PREFS_ERR_NVS_FAIL;
    }
    
    *buf_size = required_size;
    return PREFS_OK;
}

prefs_result_t prefs_set_blob(prefs_category_t category, const char *key, 
                              const void *data, size_t size)
{
    if (!prefs_state.initialized) {
        return PREFS_ERR_NOT_INIT;
    }
    
    if (category >= PREFS_CAT_COUNT || !key || !data || size == 0) {
        return PREFS_ERR_INVALID_PARAM;
    }
    
    if (size > PREFS_MAX_BLOB_SIZE) {
        ESP_LOGW(TAG, "Blob too large for %s: %u bytes", key, (unsigned)size);
        return PREFS_ERR_SIZE_MISMATCH;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return PREFS_ERR_NVS_FAIL;
    }
    
    esp_err_t err = nvs_set_blob(prefs_state.handles[category], key, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write blob failed for %s: %s", key, esp_err_to_name(err));
        return PREFS_ERR_NVS_FAIL;
    }
    
    ESP_LOGD(TAG, "Set %s/%s = [%u bytes blob]", NAMESPACE_NAMES[category], key, (unsigned)size);
    return PREFS_OK;
}

/* ============================================================================
 * Cache Management
 * ============================================================================ */

bool prefs_commit(void)
{
    if (!prefs_state.initialized) {
        return false;
    }
    
    bool all_ok = true;
    
    for (int cat = 0; cat < PREFS_CAT_COUNT; cat++) {
        if (!prefs_state.handles_open[cat]) {
            continue;
        }
        
        // Check if any entries are dirty
        category_cache_t *cache = &prefs_state.cache[cat];
        bool has_dirty = false;
        
        for (int i = 0; i < cache->count; i++) {
            if (cache->entries[i].is_valid && cache->entries[i].is_dirty) {
                has_dirty = true;
                // Write dirty entries to NVS
                esp_err_t err = nvs_set_i32(prefs_state.handles[cat], 
                                            cache->entries[i].key, 
                                            cache->entries[i].value_int);
                if (err == ESP_OK) {
                    cache->entries[i].is_dirty = false;
                } else {
                    ESP_LOGE(TAG, "Failed to commit %s: %s", 
                             cache->entries[i].key, esp_err_to_name(err));
                    all_ok = false;
                }
            }
        }
        
        // Commit NVS changes to flash
        if (has_dirty) {
            esp_err_t err = nvs_commit(prefs_state.handles[cat]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "NVS commit failed for %s: %s", 
                         NAMESPACE_NAMES[cat], esp_err_to_name(err));
                all_ok = false;
            }
        }
    }
    
    return all_ok;
}

void prefs_cache_clear(void)
{
    if (!prefs_state.initialized) {
        return;
    }
    
    for (int cat = 0; cat < PREFS_CAT_COUNT; cat++) {
        memset(&prefs_state.cache[cat], 0, sizeof(category_cache_t));
    }
    
    ESP_LOGD(TAG, "Cache cleared");
}

int prefs_cache_preload(prefs_category_t category)
{
    // Pre-load commonly used keys for a category
    // This is called at startup to avoid NVS reads during playback
    
    if (!prefs_state.initialized || category >= PREFS_CAT_COUNT) {
        return 0;
    }
    
    int loaded = 0;
    int32_t value;
    
    switch (category) {
        case PREFS_CAT_AUDIO:
            prefs_get_int(category, PREFS_KEY_AUDIO_MASTER_VOL, &value, 
                          PREFS_DEFAULT_AUDIO_MASTER_VOL);
            loaded++;
            prefs_get_int(category, PREFS_KEY_AUDIO_CUE_VOL, &value, 
                          PREFS_DEFAULT_AUDIO_CUE_VOL);
            loaded++;
            prefs_get_int(category, PREFS_KEY_AUDIO_LIMITER, &value, 
                          PREFS_DEFAULT_AUDIO_LIMITER);
            loaded++;
            break;
            
        case PREFS_CAT_DISPLAY:
            prefs_get_int(category, PREFS_KEY_DISP_BRIGHTNESS, &value, 
                          PREFS_DEFAULT_DISP_BRIGHTNESS);
            loaded++;
            prefs_get_int(category, PREFS_KEY_DISP_WAVEFORM_ZOOM, &value, 
                          PREFS_DEFAULT_DISP_WAVEFORM_ZOOM);
            loaded++;
            prefs_get_int(category, PREFS_KEY_DISP_SHOW_GRID, &value, 
                          PREFS_DEFAULT_DISP_SHOW_GRID);
            loaded++;
            break;
            
        case PREFS_CAT_CONTROLS:
            prefs_get_int(category, PREFS_KEY_CTRL_JOG_SENS, &value, 
                          PREFS_DEFAULT_CTRL_JOG_SENS);
            loaded++;
            prefs_get_int(category, PREFS_KEY_CTRL_PITCH_RANGE, &value, 
                          PREFS_DEFAULT_CTRL_PITCH_RANGE);
            loaded++;
            prefs_get_int(category, PREFS_KEY_CTRL_QUANTIZE, &value, 
                          PREFS_DEFAULT_CTRL_QUANTIZE);
            loaded++;
            break;
            
        case PREFS_CAT_THEME:
            {
                uint32_t uvalue;
                prefs_get_uint(category, PREFS_KEY_THEME_PRIMARY, &uvalue, 
                               PREFS_DEFAULT_THEME_PRIMARY);
                loaded++;
                prefs_get_uint(category, PREFS_KEY_THEME_WAVEFORM, &uvalue, 
                               PREFS_DEFAULT_THEME_WAVEFORM);
                loaded++;
            }
            break;
            
        default:
            break;
    }
    
    ESP_LOGD(TAG, "Preloaded %d values for category %d", loaded, category);
    return loaded;
}

/* ============================================================================
 * Reset and Defaults
 * ============================================================================ */

bool prefs_reset_category(prefs_category_t category)
{
    if (!prefs_state.initialized || category >= PREFS_CAT_COUNT) {
        return false;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return false;
    }
    
    esp_err_t err = nvs_erase_all(prefs_state.handles[category]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase category %s: %s", 
                 NAMESPACE_NAMES[category], esp_err_to_name(err));
        return false;
    }
    
    err = nvs_commit(prefs_state.handles[category]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase for %s: %s", 
                 NAMESPACE_NAMES[category], esp_err_to_name(err));
        return false;
    }
    
    // Clear cache for this category
    memset(&prefs_state.cache[category], 0, sizeof(category_cache_t));
    
    ESP_LOGI(TAG, "Reset category %s to defaults", NAMESPACE_NAMES[category]);
    return true;
}

bool prefs_reset_all(void)
{
    bool all_ok = true;
    
    for (int i = 0; i < PREFS_CAT_COUNT; i++) {
        if (!prefs_reset_category(i)) {
            all_ok = false;
        }
    }
    
    ESP_LOGI(TAG, "Reset all preferences to defaults");
    return all_ok;
}

bool prefs_key_exists(prefs_category_t category, const char *key)
{
    if (!prefs_state.initialized || category >= PREFS_CAT_COUNT || !key) {
        return false;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return false;
    }
    
    // Try to get the value - any type will do for existence check
    int32_t dummy;
    esp_err_t err = nvs_get_i32(prefs_state.handles[category], key, &dummy);
    
    if (err == ESP_OK) {
        return true;
    }
    
    // Try string
    size_t size = 0;
    err = nvs_get_str(prefs_state.handles[category], key, NULL, &size);
    if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) {
        return true;
    }
    
    // Try blob
    size = 0;
    err = nvs_get_blob(prefs_state.handles[category], key, NULL, &size);
    if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) {
        return true;
    }
    
    return false;
}

bool prefs_erase_key(prefs_category_t category, const char *key)
{
    if (!prefs_state.initialized || category >= PREFS_CAT_COUNT || !key) {
        return false;
    }
    
    if (ensure_handle_open(category) != ESP_OK) {
        return false;
    }
    
    // Invalidate cache entry
    cache_invalidate(category, key);
    
    esp_err_t err = nvs_erase_key(prefs_state.handles[category], key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase key %s: %s", key, esp_err_to_name(err));
        return false;
    }
    
    nvs_commit(prefs_state.handles[category]);
    
    ESP_LOGD(TAG, "Erased key %s/%s", NAMESPACE_NAMES[category], key);
    return true;
}

/* ============================================================================
 * Convenience Getters/Setters
 * ============================================================================ */

int32_t prefs_get_master_volume(void)
{
    int32_t value;
    prefs_get_int(PREFS_CAT_AUDIO, PREFS_KEY_AUDIO_MASTER_VOL, &value, 
                  PREFS_DEFAULT_AUDIO_MASTER_VOL);
    return value;
}

void prefs_set_master_volume(int32_t volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    prefs_set_int(PREFS_CAT_AUDIO, PREFS_KEY_AUDIO_MASTER_VOL, volume);
}

uint8_t prefs_get_brightness(void)
{
    int32_t value;
    prefs_get_int(PREFS_CAT_DISPLAY, PREFS_KEY_DISP_BRIGHTNESS, &value, 
                  PREFS_DEFAULT_DISP_BRIGHTNESS);
    return (uint8_t)value;
}

void prefs_set_brightness(uint8_t brightness)
{
    prefs_set_int(PREFS_CAT_DISPLAY, PREFS_KEY_DISP_BRIGHTNESS, (int32_t)brightness);
}

int32_t prefs_get_jog_sensitivity(void)
{
    int32_t value;
    prefs_get_int(PREFS_CAT_CONTROLS, PREFS_KEY_CTRL_JOG_SENS, &value, 
                  PREFS_DEFAULT_CTRL_JOG_SENS);
    return value;
}

void prefs_set_jog_sensitivity(int32_t sensitivity)
{
    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 10) sensitivity = 10;
    prefs_set_int(PREFS_CAT_CONTROLS, PREFS_KEY_CTRL_JOG_SENS, sensitivity);
}

int32_t prefs_get_pitch_range(void)
{
    int32_t value;
    prefs_get_int(PREFS_CAT_CONTROLS, PREFS_KEY_CTRL_PITCH_RANGE, &value, 
                  PREFS_DEFAULT_CTRL_PITCH_RANGE);
    return value;
}

void prefs_set_pitch_range(int32_t range)
{
    // Valid ranges: 4, 8, 16, 50
    if (range != 4 && range != 8 && range != 16 && range != 50) {
        range = 8;  // Default to 8%
    }
    prefs_set_int(PREFS_CAT_CONTROLS, PREFS_KEY_CTRL_PITCH_RANGE, range);
}

bool prefs_get_quantize_enabled(void)
{
    int32_t value;
    prefs_get_int(PREFS_CAT_CONTROLS, PREFS_KEY_CTRL_QUANTIZE, &value, 
                  PREFS_DEFAULT_CTRL_QUANTIZE);
    return value != 0;
}

void prefs_set_quantize_enabled(bool enabled)
{
    prefs_set_int(PREFS_CAT_CONTROLS, PREFS_KEY_CTRL_QUANTIZE, enabled ? 1 : 0);
}

uint32_t prefs_get_waveform_color(void)
{
    uint32_t value;
    prefs_get_uint(PREFS_CAT_THEME, PREFS_KEY_THEME_WAVEFORM, &value, 
                   PREFS_DEFAULT_THEME_WAVEFORM);
    return value;
}

void prefs_set_waveform_color(uint32_t color)
{
    prefs_set_uint(PREFS_CAT_THEME, PREFS_KEY_THEME_WAVEFORM, color);
}
