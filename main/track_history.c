/**
 * @file track_history.c
 * @brief Track play history tracking implementation
 */

#define _GNU_SOURCE  // For strcasestr on GNU systems
#include "track_history.h"
#include "storage.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

// Portable strcasestr implementation (in case not available)
#ifndef HAVE_STRCASESTR
static char *local_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    
    size_t needle_len = strlen(needle);
    
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}
#define strcasestr local_strcasestr
#endif

static const char *TAG = "track_history";

// File header for persistence
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t recent_head;       // Index of most recent play in recent_order
    uint32_t recent_count;      // Number of entries in recent list
    uint32_t reserved[3];       // Future use
} history_file_header_t;
#pragma pack(pop)

// Internal state
static track_history_entry_t s_entries[TRACK_HISTORY_MAX_TRACKED];
static uint32_t s_entry_count = 0;

// Recent plays: circular buffer of indices into s_entries
static uint16_t s_recent_order[TRACK_HISTORY_MAX_RECENT];
static uint32_t s_recent_head = 0;  // Points to most recent
static uint32_t s_recent_count = 0;

static bool s_initialized = false;
static bool s_dirty = false;

/**
 * @brief FNV-1a hash function for strings
 * 
 * Fast, good distribution for file paths.
 */
static uint32_t fnv1a_hash(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

/**
 * @brief Store truncated path (keeps end portion for uniqueness)
 */
static void store_short_path(const char *full_path, char *short_path, size_t max_len) {
    size_t full_len = strlen(full_path);
    
    if (full_len < max_len) {
        strcpy(short_path, full_path);
    } else {
        // Keep the end of the path (more unique - includes filename)
        const char *src = full_path + full_len - (max_len - 1);
        strcpy(short_path, src);
    }
}

/**
 * @brief Find entry by path hash
 * 
 * @return Index in s_entries, or -1 if not found
 */
static int find_entry_by_hash(uint32_t hash) {
    for (uint32_t i = 0; i < s_entry_count; i++) {
        if (s_entries[i].path_hash == hash) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Create parent directories for a path
 */
static bool ensure_parent_dir(const char *filepath) {
    char *path_copy = strdup(filepath);
    if (!path_copy) return false;
    
    char *last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        
        struct stat st;
        if (stat(path_copy, &st) != 0) {
            // Directory doesn't exist, create it
            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                ESP_LOGW(TAG, "Failed to create directory: %s", path_copy);
                free(path_copy);
                return false;
            }
        }
    }
    
    free(path_copy);
    return true;
}

/**
 * @brief Load history from file
 */
static bool load_history(void) {
    FILE *f = fopen(TRACK_HISTORY_FILE, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No existing history file found");
        return true;  // Not an error - just no history yet
    }
    
    // Read header
    history_file_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        ESP_LOGW(TAG, "Failed to read history header");
        fclose(f);
        return false;
    }
    
    // Validate header
    if (header.magic != TRACK_HISTORY_MAGIC) {
        ESP_LOGW(TAG, "Invalid history file magic: 0x%08X", header.magic);
        fclose(f);
        return false;
    }
    
    if (header.version != TRACK_HISTORY_VERSION) {
        ESP_LOGW(TAG, "History file version mismatch: %u (expected %u)", 
                 header.version, TRACK_HISTORY_VERSION);
        fclose(f);
        return false;
    }
    
    // Clamp entry count to our maximum
    uint32_t entries_to_read = header.entry_count;
    if (entries_to_read > TRACK_HISTORY_MAX_TRACKED) {
        ESP_LOGW(TAG, "History file has %u entries, truncating to %u",
                 entries_to_read, TRACK_HISTORY_MAX_TRACKED);
        entries_to_read = TRACK_HISTORY_MAX_TRACKED;
    }
    
    // Read entries
    if (entries_to_read > 0) {
        if (fread(s_entries, sizeof(track_history_entry_t), entries_to_read, f) != entries_to_read) {
            ESP_LOGW(TAG, "Failed to read history entries");
            fclose(f);
            return false;
        }
    }
    s_entry_count = entries_to_read;
    
    // Read recent order
    uint32_t recent_to_read = header.recent_count;
    if (recent_to_read > TRACK_HISTORY_MAX_RECENT) {
        recent_to_read = TRACK_HISTORY_MAX_RECENT;
    }
    
    if (recent_to_read > 0) {
        if (fread(s_recent_order, sizeof(uint16_t), recent_to_read, f) != recent_to_read) {
            ESP_LOGW(TAG, "Failed to read recent order");
            // Non-fatal - we can rebuild recent list
            s_recent_count = 0;
            s_recent_head = 0;
        } else {
            s_recent_count = recent_to_read;
            s_recent_head = header.recent_head;
            if (s_recent_head >= TRACK_HISTORY_MAX_RECENT) {
                s_recent_head = 0;
            }
        }
    }
    
    fclose(f);
    ESP_LOGI(TAG, "Loaded history: %u tracks, %u recent", s_entry_count, s_recent_count);
    return true;
}

/**
 * @brief Save history to file
 */
static bool save_history(void) {
    if (!storage_is_available()) {
        ESP_LOGW(TAG, "Cannot save history - no storage available");
        return false;
    }
    
    // Ensure parent directory exists
    if (!ensure_parent_dir(TRACK_HISTORY_FILE)) {
        return false;
    }
    
    FILE *f = fopen(TRACK_HISTORY_FILE, "wb");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open history file for writing: %s", strerror(errno));
        return false;
    }
    
    // Write header
    history_file_header_t header = {
        .magic = TRACK_HISTORY_MAGIC,
        .version = TRACK_HISTORY_VERSION,
        .entry_count = s_entry_count,
        .recent_head = s_recent_head,
        .recent_count = s_recent_count,
        .reserved = {0}
    };
    
    if (fwrite(&header, sizeof(header), 1, f) != 1) {
        ESP_LOGW(TAG, "Failed to write history header");
        fclose(f);
        return false;
    }
    
    // Write entries
    if (s_entry_count > 0) {
        if (fwrite(s_entries, sizeof(track_history_entry_t), s_entry_count, f) != s_entry_count) {
            ESP_LOGW(TAG, "Failed to write history entries");
            fclose(f);
            return false;
        }
    }
    
    // Write recent order
    if (s_recent_count > 0) {
        if (fwrite(s_recent_order, sizeof(uint16_t), s_recent_count, f) != s_recent_count) {
            ESP_LOGW(TAG, "Failed to write recent order");
            fclose(f);
            return false;
        }
    }
    
    fclose(f);
    s_dirty = false;
    ESP_LOGD(TAG, "Saved history: %u tracks, %u recent", s_entry_count, s_recent_count);
    return true;
}

/**
 * @brief Add entry index to recent list
 */
static void add_to_recent(uint16_t entry_idx) {
    // Move head forward (circular)
    s_recent_head = (s_recent_head + 1) % TRACK_HISTORY_MAX_RECENT;
    s_recent_order[s_recent_head] = entry_idx;
    
    if (s_recent_count < TRACK_HISTORY_MAX_RECENT) {
        s_recent_count++;
    }
}

/**
 * @brief Comparison function for sorting by play count (descending)
 */
static int compare_by_play_count(const void *a, const void *b) {
    const track_history_entry_t *ea = (const track_history_entry_t *)a;
    const track_history_entry_t *eb = (const track_history_entry_t *)b;
    
    if (ea->play_count > eb->play_count) return -1;
    if (ea->play_count < eb->play_count) return 1;
    return 0;
}

/**
 * @brief Comparison function for sorting by last played (descending)
 */
static int compare_by_last_played(const void *a, const void *b) {
    const track_history_entry_t *ea = (const track_history_entry_t *)a;
    const track_history_entry_t *eb = (const track_history_entry_t *)b;
    
    if (ea->last_played > eb->last_played) return -1;
    if (ea->last_played < eb->last_played) return 1;
    return 0;
}

// Public API implementation

bool track_history_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Track history already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing track history");
    
    // Clear state
    memset(s_entries, 0, sizeof(s_entries));
    memset(s_recent_order, 0, sizeof(s_recent_order));
    s_entry_count = 0;
    s_recent_head = 0;
    s_recent_count = 0;
    s_dirty = false;
    
    // Load existing history
    if (!load_history()) {
        ESP_LOGW(TAG, "Failed to load history, starting fresh");
    }
    
    s_initialized = true;
    return true;
}

void track_history_deinit(void) {
    if (!s_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing track history");
    
    if (s_dirty) {
        save_history();
    }
    
    s_initialized = false;
}

bool track_history_record_play(const char *filepath) {
    if (!s_initialized || !filepath) {
        return false;
    }
    
    uint32_t hash = fnv1a_hash(filepath);
    time_t now = time(NULL);
    
    // Find existing entry
    int idx = find_entry_by_hash(hash);
    
    if (idx >= 0) {
        // Update existing entry
        s_entries[idx].play_count++;
        s_entries[idx].last_played = now;
        add_to_recent((uint16_t)idx);
        
        ESP_LOGD(TAG, "Updated play count for %s: %u", 
                 s_entries[idx].path_short, s_entries[idx].play_count);
    } else {
        // Create new entry
        if (s_entry_count >= TRACK_HISTORY_MAX_TRACKED) {
            // Find and replace least played track
            uint32_t min_plays = UINT32_MAX;
            uint32_t min_idx = 0;
            
            for (uint32_t i = 0; i < s_entry_count; i++) {
                if (s_entries[i].play_count < min_plays) {
                    min_plays = s_entries[i].play_count;
                    min_idx = i;
                }
            }
            
            idx = (int)min_idx;
            ESP_LOGD(TAG, "Replacing least played track at index %d", idx);
        } else {
            idx = (int)s_entry_count++;
        }
        
        // Initialize new entry
        track_history_entry_t *entry = &s_entries[idx];
        entry->path_hash = hash;
        store_short_path(filepath, entry->path_short, TRACK_HISTORY_PATH_LEN);
        entry->play_count = 1;
        entry->last_played = now;
        entry->first_played = now;
        
        add_to_recent((uint16_t)idx);
        
        ESP_LOGD(TAG, "Added new track to history: %s", entry->path_short);
    }
    
    s_dirty = true;
    
    // Auto-save periodically (every 5 changes)
    static uint32_t change_count = 0;
    if (++change_count >= 5) {
        save_history();
        change_count = 0;
    }
    
    return true;
}

uint32_t track_history_get_recent(track_history_entry_t *entries, uint32_t max_count) {
    if (!s_initialized || !entries || max_count == 0) {
        return 0;
    }
    
    uint32_t count = 0;
    
    // Walk backwards from head
    for (uint32_t i = 0; i < s_recent_count && count < max_count; i++) {
        uint32_t idx = (s_recent_head + TRACK_HISTORY_MAX_RECENT - i) % TRACK_HISTORY_MAX_RECENT;
        uint16_t entry_idx = s_recent_order[idx];
        
        if (entry_idx < s_entry_count) {
            entries[count++] = s_entries[entry_idx];
        }
    }
    
    return count;
}

uint32_t track_history_get_most_played(track_history_entry_t *entries, uint32_t max_count) {
    if (!s_initialized || !entries || max_count == 0) {
        return 0;
    }
    
    // Copy entries for sorting
    uint32_t copy_count = (s_entry_count < max_count) ? s_entry_count : max_count;
    
    // Allocate temporary array for sorting all entries
    track_history_entry_t *temp = (track_history_entry_t *)malloc(
        s_entry_count * sizeof(track_history_entry_t));
    
    if (!temp) {
        ESP_LOGW(TAG, "Failed to allocate temp buffer for sorting");
        return 0;
    }
    
    memcpy(temp, s_entries, s_entry_count * sizeof(track_history_entry_t));
    qsort(temp, s_entry_count, sizeof(track_history_entry_t), compare_by_play_count);
    
    memcpy(entries, temp, copy_count * sizeof(track_history_entry_t));
    free(temp);
    
    return copy_count;
}

bool track_history_get_track(const char *filepath, track_history_entry_t *entry) {
    if (!s_initialized || !filepath || !entry) {
        return false;
    }
    
    uint32_t hash = fnv1a_hash(filepath);
    int idx = find_entry_by_hash(hash);
    
    if (idx >= 0) {
        *entry = s_entries[idx];
        return true;
    }
    
    return false;
}

uint32_t track_history_search(const char *query, track_history_entry_t *entries, uint32_t max_count) {
    if (!s_initialized || !query || !entries || max_count == 0) {
        return 0;
    }
    
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < s_entry_count && count < max_count; i++) {
        // Case-insensitive substring search
        if (strcasestr(s_entries[i].path_short, query) != NULL) {
            entries[count++] = s_entries[i];
        }
    }
    
    return count;
}

uint32_t track_history_get_play_count(const char *filepath) {
    if (!s_initialized || !filepath) {
        return 0;
    }
    
    uint32_t hash = fnv1a_hash(filepath);
    int idx = find_entry_by_hash(hash);
    
    if (idx >= 0) {
        return s_entries[idx].play_count;
    }
    
    return 0;
}

uint32_t track_history_get_total_tracks(void) {
    if (!s_initialized) {
        return 0;
    }
    return s_entry_count;
}

uint32_t track_history_get_total_plays(void) {
    if (!s_initialized) {
        return 0;
    }
    
    uint32_t total = 0;
    for (uint32_t i = 0; i < s_entry_count; i++) {
        total += s_entries[i].play_count;
    }
    return total;
}

bool track_history_clear(void) {
    if (!s_initialized) {
        return false;
    }
    
    ESP_LOGI(TAG, "Clearing track history");
    
    memset(s_entries, 0, sizeof(s_entries));
    memset(s_recent_order, 0, sizeof(s_recent_order));
    s_entry_count = 0;
    s_recent_head = 0;
    s_recent_count = 0;
    
    // Delete history file
    remove(TRACK_HISTORY_FILE);
    
    s_dirty = false;
    return true;
}

bool track_history_remove_track(const char *filepath) {
    if (!s_initialized || !filepath) {
        return false;
    }
    
    uint32_t hash = fnv1a_hash(filepath);
    int idx = find_entry_by_hash(hash);
    
    if (idx < 0) {
        return false;
    }
    
    ESP_LOGD(TAG, "Removing track from history: %s", s_entries[idx].path_short);
    
    // Shift remaining entries
    for (uint32_t i = (uint32_t)idx; i < s_entry_count - 1; i++) {
        s_entries[i] = s_entries[i + 1];
    }
    s_entry_count--;
    
    // Update recent order indices
    for (uint32_t i = 0; i < s_recent_count; i++) {
        if (s_recent_order[i] == (uint16_t)idx) {
            s_recent_order[i] = UINT16_MAX;  // Mark as invalid
        } else if (s_recent_order[i] > (uint16_t)idx) {
            s_recent_order[i]--;  // Shift index down
        }
    }
    
    s_dirty = true;
    return true;
}

bool track_history_save(void) {
    if (!s_initialized) {
        return false;
    }
    return save_history();
}

bool track_history_is_dirty(void) {
    return s_dirty;
}

uint32_t track_history_get_plays_since(uint32_t hours) {
    if (!s_initialized) {
        return 0;
    }
    
    time_t now = time(NULL);
    time_t cutoff = now - (hours * 3600);
    
    uint32_t count = 0;
    
    // Walk through recent list and count plays after cutoff
    for (uint32_t i = 0; i < s_recent_count; i++) {
        uint32_t idx = (s_recent_head + TRACK_HISTORY_MAX_RECENT - i) % TRACK_HISTORY_MAX_RECENT;
        uint16_t entry_idx = s_recent_order[idx];
        
        if (entry_idx < s_entry_count) {
            if (s_entries[entry_idx].last_played >= cutoff) {
                count++;
            } else {
                // Since recent list is ordered by time, we can stop here
                break;
            }
        }
    }
    
    return count;
}
