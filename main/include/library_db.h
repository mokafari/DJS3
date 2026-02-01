/**
 * @file library_db.h
 * @brief Library database for fast track browsing
 * 
 * Provides a compact index of all analyzed tracks for:
 * - Fast boot (single file read vs scanning 1000+ files)
 * - BPM/key sorting without opening individual .odk files
 * - Background verification of database integrity
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Magic number for library.db file
#define LIBRARY_MAGIC   0x4C494231  // "LIB1"
#define LIBRARY_VERSION 1

// Maximum entries in library
#define MAX_LIBRARY_ENTRIES 2000

/**
 * @brief Library entry flags
 */
typedef enum {
    LIB_FLAG_ANALYZED   = 0x01,  ///< Track has complete .odk analysis
    LIB_FLAG_HAS_CUES   = 0x02,  ///< Track has hot cues set
    LIB_FLAG_VERIFIED   = 0x04,  ///< Entry verified against filesystem
    LIB_FLAG_FAVORITE   = 0x08,  ///< User marked as favorite
} library_flags_t;

/**
 * @brief Compact library entry (8 bytes per track)
 */
typedef struct {
    uint32_t path_hash;     ///< CRC32 of full path for fast lookup
    uint16_t bpm_x100;      ///< BPM * 100 (e.g., 12800 = 128.00 BPM)
    uint8_t  key_id;        ///< Camelot key (0-23)
    uint8_t  flags;         ///< library_flags_t bitfield
} LibraryEntry_t;

/**
 * @brief Library database header
 */
typedef struct {
    uint32_t magic;             ///< LIBRARY_MAGIC
    uint32_t version;           ///< LIBRARY_VERSION
    uint32_t entry_count;       ///< Number of valid entries
    uint32_t last_scan_time;    ///< Unix timestamp of last scan
} LibraryHeader_t;

/**
 * @brief Initialize library database
 * 
 * Must be called before other library functions.
 */
void library_db_init(void);

/**
 * @brief Load library database from SD card
 * 
 * Fast operation (~1ms for 1000 entries).
 * 
 * @return true if loaded successfully, false if file missing/corrupt
 */
bool library_db_load(void);

/**
 * @brief Save library database to SD card
 * 
 * @return true on success
 */
bool library_db_save(void);

/**
 * @brief Rebuild library by scanning all .odk files
 * 
 * Slow operation - scans entire .opendeck directory.
 * Should be called in background or when explicitly requested.
 * 
 * @return Number of entries found
 */
uint32_t library_db_rebuild(void);

/**
 * @brief Start background verification of library
 * 
 * Verifies each entry against actual .odk files.
 * Runs at low priority.
 */
void library_db_verify_async(void);

/**
 * @brief Find entry by path hash
 * 
 * @param path_hash CRC32 hash of file path
 * @return Pointer to entry or NULL if not found
 */
LibraryEntry_t* library_db_find(uint32_t path_hash);

/**
 * @brief Find entry by file path
 * 
 * @param filepath Full path to MP3 file
 * @return Pointer to entry or NULL if not found
 */
LibraryEntry_t* library_db_find_by_path(const char *filepath);

/**
 * @brief Add or update library entry
 * 
 * @param filepath Full path to MP3 file
 * @param bpm      BPM value
 * @param key_id   Camelot key (0-23)
 * @param flags    Entry flags
 * @return true on success
 */
bool library_db_update(const char *filepath, float bpm, uint8_t key_id, uint8_t flags);

/**
 * @brief Remove entry from library
 * 
 * @param filepath Full path to MP3 file
 * @return true if removed
 */
bool library_db_remove(const char *filepath);

/**
 * @brief Get number of entries in library
 * 
 * @return Entry count
 */
uint32_t library_db_get_count(void);

/**
 * @brief Get entry by index
 * 
 * @param index Entry index (0 to count-1)
 * @return Pointer to entry or NULL if out of range
 */
LibraryEntry_t* library_db_get_entry(uint32_t index);

/**
 * @brief Sort library by BPM
 * 
 * @param ascending true for low-to-high, false for high-to-low
 */
void library_db_sort_by_bpm(bool ascending);

/**
 * @brief Sort library by key
 * 
 * Sorts in Camelot wheel order for harmonic mixing.
 * 
 * @param ascending true for 1A-12B, false for 12B-1A
 */
void library_db_sort_by_key(bool ascending);

/**
 * @brief Calculate CRC32 hash of a string
 * 
 * @param str String to hash
 * @return CRC32 hash value
 */
uint32_t library_db_hash(const char *str);

/**
 * @brief Check if library is loaded
 * 
 * @return true if library is loaded in RAM
 */
bool library_db_is_loaded(void);

/**
 * @brief Get last scan timestamp
 * 
 * @return Unix timestamp or 0 if not scanned
 */
uint32_t library_db_get_last_scan(void);

#ifdef __cplusplus
}
#endif
