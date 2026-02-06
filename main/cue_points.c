/**
 * @file cue_points.c
 * @brief Cue point system implementation with .odk persistence
 * 
 * Hot cue points are stored in memory and persisted to .odk metadata files.
 * All save operations are mutex-protected to prevent corruption during
 * concurrent access from analyzer and UI.
 */

#include "cue_points.h"
#include "metadata.h"
#include "metadata_format.h"
#include "waveform.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "cue_points";

// In-memory cue point storage
static HotCue_t cue_points[MAX_CUE_POINTS] = {0};

// Current track path for persistence
static char current_track_path[256] = {0};

// Default colors for each cue index
static const uint16_t default_colors[MAX_CUE_POINTS] = {
    CUE_COLOR_RED,     // Cue 1
    CUE_COLOR_ORANGE,  // Cue 2
    CUE_COLOR_YELLOW,  // Cue 3
    CUE_COLOR_GREEN,   // Cue 4
    CUE_COLOR_CYAN,    // Cue 5
    CUE_COLOR_BLUE,    // Cue 6
    CUE_COLOR_PURPLE,  // Cue 7
    CUE_COLOR_WHITE    // Cue 8
};

/**
 * @brief Initialize cue points for a track
 */
void cue_points_init_for_track(const char *filepath) {
    // Clear existing cue points
    memset(cue_points, 0, sizeof(cue_points));
    memset(current_track_path, 0, sizeof(current_track_path));
    
    if (!filepath || strlen(filepath) == 0) {
        ESP_LOGW(TAG, "No filepath provided, cue points cleared");
        return;
    }
    
    // Store path for persistence
    strncpy(current_track_path, filepath, sizeof(current_track_path) - 1);
    
    // Try to load cue points from metadata
    TrackMetadata_t meta;
    if (metadata_load(filepath, &meta)) {
        // Copy hot cues from metadata
        for (int i = 0; i < MAX_CUE_POINTS && i < NUM_HOTCUES; i++) {
            cue_points[i] = meta.hotcues[i];
        }
        
        // Count how many are set
        int count = 0;
        for (int i = 0; i < MAX_CUE_POINTS; i++) {
            if (cue_points[i].active) count++;
        }
        
        ESP_LOGI(TAG, "Loaded %d cue points from metadata", count);
    } else {
        // Initialize with default colors
        for (int i = 0; i < MAX_CUE_POINTS; i++) {
            cue_points[i].color_rgb565 = default_colors[i];
        }
        ESP_LOGI(TAG, "No metadata, cue points initialized with defaults");
    }
}

/**
 * @brief Set cue point (milliseconds)
 */
bool cue_points_set_ms(uint8_t cue_index, uint32_t position_ms) {
    if (cue_index >= MAX_CUE_POINTS) {
        ESP_LOGE(TAG, "Invalid cue index: %d", cue_index);
        return false;
    }
    
    cue_points[cue_index].position_ms = position_ms;
    cue_points[cue_index].active = 1;
    
    // Set default color if not already set
    if (cue_points[cue_index].color_rgb565 == 0) {
        cue_points[cue_index].color_rgb565 = default_colors[cue_index];
    }
    
    ESP_LOGI(TAG, "Cue %d set at %u ms (color: 0x%04X)", 
             cue_index, position_ms, cue_points[cue_index].color_rgb565);
    
    // Update waveform marker
    waveform_add_cue_marker(cue_index, position_ms / 1000, cue_points[cue_index].color_rgb565);
    
    // Persist to metadata
    cue_points_save();
    
    return true;
}

/**
 * @brief Set cue point (legacy, seconds)
 */
bool cue_points_set(uint8_t cue_index, uint32_t position) {
    return cue_points_set_ms(cue_index, position * 1000);
}

/**
 * @brief Get cue point position (milliseconds)
 */
uint32_t cue_points_get_ms(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS || !cue_points[cue_index].active) {
        return 0;
    }
    return cue_points[cue_index].position_ms;
}

/**
 * @brief Get cue point position (legacy, seconds)
 */
uint32_t cue_points_get(uint8_t cue_index) {
    return cue_points_get_ms(cue_index) / 1000;
}

/**
 * @brief Check if cue is set
 */
bool cue_points_is_set(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        return false;
    }
    return cue_points[cue_index].active != 0;
}

/**
 * @brief Get cue color
 */
uint16_t cue_points_get_color(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        return CUE_COLOR_WHITE;
    }
    return cue_points[cue_index].color_rgb565;
}

/**
 * @brief Set cue color
 */
void cue_points_set_color(uint8_t cue_index, uint16_t color) {
    if (cue_index >= MAX_CUE_POINTS) {
        return;
    }
    cue_points[cue_index].color_rgb565 = color;
    
    // Update waveform marker color if cue is active
    if (cue_points[cue_index].active) {
        waveform_add_cue_marker(cue_index, cue_points[cue_index].position_ms / 1000, color);
        cue_points_save();
    }
}

/**
 * @brief Clear a cue point
 */
void cue_points_clear(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        return;
    }
    
    cue_points[cue_index].active = 0;
    cue_points[cue_index].position_ms = 0;
    // Keep color for next use
    
    ESP_LOGI(TAG, "Cue %d cleared", cue_index);
    
    // Remove waveform marker
    waveform_remove_cue_marker(cue_index);
    
    // Persist change
    cue_points_save();
}

/**
 * @brief Clear all cue points
 */
void cue_points_clear_all(void) {
    for (int i = 0; i < MAX_CUE_POINTS; i++) {
        cue_points[i].active = 0;
        cue_points[i].position_ms = 0;
    }
    
    // Clear all waveform markers
    waveform_clear_cue_markers();
    
    ESP_LOGI(TAG, "All cue points cleared");
    
    // Persist change
    cue_points_save();
}

/**
 * @brief Save cue points to metadata file
 * 
 * If no metadata exists yet, creates a minimal .odk file with just
 * the cue point data. The analyzer will fill in waveform/BPM later.
 */
bool cue_points_save(void) {
    if (current_track_path[0] == '\0') {
        ESP_LOGW(TAG, "No track path set, cannot save cue points");
        return false;
    }
    
    TrackMetadata_t meta;
    bool metadata_existed = metadata_load(current_track_path, &meta);
    
    if (!metadata_existed) {
        // Create new minimal metadata for this track
        ESP_LOGI(TAG, "Creating new metadata for cue points: %s", current_track_path);
        
        memset(&meta, 0, sizeof(meta));
        meta.magic = ODK_MAGIC;
        meta.version = ODK_VERSION;
        
        // Get source file size for change detection
        struct stat st;
        if (stat(current_track_path, &st) == 0) {
            meta.source_size = st.st_size;
        }
        
        // Set defaults - analyzer will update these later
        meta.bpm = 120.0f;  // Default BPM
        meta.key_id = 255;  // Unknown key
        meta.duration_ms = 0;  // Will be calculated by analyzer
        meta.grid_offset = 0;
        
        // Initialize seek table with linear estimate (will be refined by Pass 1)
        for (int i = 0; i < SEEK_POINTS; i++) {
            meta.seek_table[i] = (meta.source_size * i) / SEEK_POINTS;
        }
        
        // Waveform stays zeroed - will be filled by Pass 2
    }
    
    // Update hot cues
    for (int i = 0; i < MAX_CUE_POINTS && i < NUM_HOTCUES; i++) {
        meta.hotcues[i] = cue_points[i];
    }
    
    // Save
    if (metadata_save(current_track_path, &meta)) {
        ESP_LOGI(TAG, "Cue points saved to metadata%s", 
                 metadata_existed ? "" : " (new file created)");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to save cue points");
        return false;
    }
}

// ============================================================================
// Extended API Implementation
// ============================================================================

// Global trigger mode
static cue_trigger_mode_t s_trigger_mode = CUE_MODE_PLAY;

// Preview state for PREVIEW mode
static uint32_t s_preview_return_position = 0;
static bool s_preview_active = false;

/**
 * @brief Get default color for a cue index
 */
uint16_t cue_points_get_default_color(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        return CUE_COLOR_WHITE;
    }
    return default_colors[cue_index];
}

/**
 * @brief Set global trigger mode for hot cues
 */
void cue_points_set_trigger_mode(cue_trigger_mode_t mode) {
    s_trigger_mode = mode;
    ESP_LOGI(TAG, "Trigger mode set to %d (%s)", mode,
             mode == CUE_MODE_JUMP ? "JUMP" :
             mode == CUE_MODE_PLAY ? "PLAY" : "PREVIEW");
}

/**
 * @brief Get current trigger mode
 */
cue_trigger_mode_t cue_points_get_trigger_mode(void) {
    return s_trigger_mode;
}

/**
 * @brief Trigger a hot cue
 */
uint32_t cue_points_trigger(uint8_t cue_index, uint32_t current_position_ms) {
    if (cue_index >= MAX_CUE_POINTS) {
        ESP_LOGE(TAG, "Invalid cue index: %d", cue_index);
        return 0;
    }
    
    // If cue is not set, set it at current position
    if (!cue_points[cue_index].active) {
        cue_points_set_ms(cue_index, current_position_ms);
        ESP_LOGI(TAG, "Hot cue %d set at %u ms", cue_index + 1, current_position_ms);
        return 0;  // Return 0 to indicate cue was set, not triggered
    }
    
    uint32_t cue_position = cue_points[cue_index].position_ms;
    
    // Handle preview mode - save return position
    if (s_trigger_mode == CUE_MODE_PREVIEW) {
        s_preview_return_position = current_position_ms;
        s_preview_active = true;
    }
    
    ESP_LOGI(TAG, "Hot cue %d triggered at %u ms (mode: %s)", 
             cue_index + 1, cue_position,
             s_trigger_mode == CUE_MODE_JUMP ? "JUMP" :
             s_trigger_mode == CUE_MODE_PLAY ? "PLAY" : "PREVIEW");
    
    return cue_position;
}

/**
 * @brief Release a hot cue (for PREVIEW mode)
 */
void cue_points_release(uint8_t cue_index) {
    if (s_trigger_mode == CUE_MODE_PREVIEW && s_preview_active) {
        ESP_LOGI(TAG, "Hot cue %d released, returning to %u ms", 
                 cue_index + 1, s_preview_return_position);
        s_preview_active = false;
        // Note: The actual seek back is handled by the caller
    }
}

/**
 * @brief Delete a cue point
 */
bool cue_points_delete(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        ESP_LOGE(TAG, "Invalid cue index for delete: %d", cue_index);
        return false;
    }
    
    if (!cue_points[cue_index].active) {
        ESP_LOGW(TAG, "Hot cue %d not set, nothing to delete", cue_index + 1);
        return false;
    }
    
    ESP_LOGI(TAG, "Deleting hot cue %d (was at %u ms)", 
             cue_index + 1, cue_points[cue_index].position_ms);
    
    cue_points_clear(cue_index);
    return true;
}

/**
 * @brief Cycle cue color to next preset
 */
uint16_t cue_points_cycle_color(uint8_t cue_index) {
    if (cue_index >= MAX_CUE_POINTS) {
        return CUE_COLOR_WHITE;
    }
    
    // Color cycle order
    static const uint16_t color_cycle[] = {
        CUE_COLOR_RED,
        CUE_COLOR_ORANGE,
        CUE_COLOR_YELLOW,
        CUE_COLOR_GREEN,
        CUE_COLOR_CYAN,
        CUE_COLOR_BLUE,
        CUE_COLOR_PURPLE,
        CUE_COLOR_WHITE
    };
    static const int num_colors = sizeof(color_cycle) / sizeof(color_cycle[0]);
    
    uint16_t current = cue_points[cue_index].color_rgb565;
    int next_idx = 0;
    
    // Find current color in cycle and advance to next
    for (int i = 0; i < num_colors; i++) {
        if (color_cycle[i] == current) {
            next_idx = (i + 1) % num_colors;
            break;
        }
    }
    
    uint16_t new_color = color_cycle[next_idx];
    cue_points_set_color(cue_index, new_color);
    
    ESP_LOGI(TAG, "Hot cue %d color changed to 0x%04X", cue_index + 1, new_color);
    return new_color;
}

/**
 * @brief Get number of active cue points
 */
uint8_t cue_points_get_active_count(void) {
    uint8_t count = 0;
    for (int i = 0; i < MAX_CUE_POINTS; i++) {
        if (cue_points[i].active) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Get preview return position
 */
uint32_t cue_points_get_preview_return_position(void) {
    return s_preview_return_position;
}

/**
 * @brief Check if preview mode is active
 */
bool cue_points_is_preview_active(void) {
    return s_preview_active;
}

/**
 * @brief Sync all cue points to waveform display
 * 
 * Updates waveform markers with current cue point positions and colors.
 */
void cue_points_sync_to_waveform(uint32_t track_duration_ms) {
    // Clear existing markers
    waveform_clear_cue_markers();
    
    if (track_duration_ms == 0) {
        ESP_LOGW(TAG, "Cannot sync cue markers: track duration is 0");
        return;
    }
    
    // Add markers for active cue points
    int synced = 0;
    for (int i = 0; i < MAX_CUE_POINTS; i++) {
        if (cue_points[i].active) {
            // Convert ms position to seconds for waveform API
            uint32_t position_sec = cue_points[i].position_ms / 1000;
            waveform_add_cue_marker(i, position_sec, cue_points[i].color_rgb565);
            synced++;
        }
    }
    
    ESP_LOGI(TAG, "Synced %d cue markers to waveform", synced);
}
