/**
 * @file metadata.h
 * @brief OpenDeck metadata manager API
 * 
 * Provides mutex-protected load/save operations for .odk metadata files.
 * Handles path conversion from MP3 to sidecar location.
 */

#pragma once

#include "metadata_format.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize metadata manager
 * 
 * Creates the mutex for thread-safe file access.
 * Must be called before any other metadata functions.
 */
void metadata_init(void);

/**
 * @brief Convert MP3 path to .odk metadata path
 * 
 * Transforms: /sdcard/Music/Song.mp3 -> /sdcard/.opendeck/Music/Song.odk
 * 
 * @param mp3_path  Source MP3 file path
 * @param out_path  Output buffer for .odk path (must be at least 256 bytes)
 */
void metadata_get_path(const char *mp3_path, char *out_path);

/**
 * @brief Check if metadata file exists for a track
 * 
 * @param mp3_path  Source MP3 file path
 * @return true if .odk file exists, false otherwise
 */
bool metadata_exists(const char *mp3_path);

/**
 * @brief Load metadata for a track
 * 
 * Thread-safe: Uses mutex to prevent read during write.
 * Validates magic number and version.
 * 
 * @param mp3_path  Source MP3 file path
 * @param out_data  Pointer to TrackMetadata_t to fill
 * @return true on success, false if file doesn't exist or is corrupt
 */
bool metadata_load(const char *mp3_path, TrackMetadata_t *out_data);

/**
 * @brief Save metadata for a track
 * 
 * Thread-safe: Uses mutex to prevent concurrent writes.
 * Creates directory structure if needed.
 * 
 * @param mp3_path  Source MP3 file path
 * @param data      Pointer to TrackMetadata_t to save
 * @return true on success, false on error (lock timeout, IO error)
 */
bool metadata_save(const char *mp3_path, const TrackMetadata_t *data);

/**
 * @brief Update hot cues in metadata file
 * 
 * Convenience function that loads existing metadata, updates hot cues,
 * and saves back to disk.
 * 
 * @param mp3_path  Source MP3 file path
 * @param hotcues   Array of NUM_HOTCUES hot cue structures
 * @return true on success, false on error
 */
bool metadata_update_hotcues(const char *mp3_path, const HotCue_t *hotcues);

/**
 * @brief Get the base directory for metadata files
 * 
 * @return Path to .opendeck directory (e.g., "/sdcard/.opendeck")
 */
const char* metadata_get_base_dir(void);

#ifdef __cplusplus
}
#endif
