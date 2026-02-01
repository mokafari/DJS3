/**
 * @file metadata_format.h
 * @brief OpenDeck (.odk) metadata file format definition
 * 
 * This header defines the binary format for pre-analyzed track metadata.
 * The format is designed to be:
 * - Lightweight (~984 bytes per track)
 * - Cross-platform compatible (ESP32 and PC/Web)
 * - Instantly loadable via single fread()
 * 
 * File structure:
 * - .odk files are stored as sidecars: /sdcard/.opendeck/Music/Track.odk
 * - Mirrors the original MP3 folder structure
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Magic number: "ODK1" in ASCII (little-endian)
#define ODK_MAGIC       0x4F444B31
#define ODK_VERSION     1

// Waveform resolution (matches display width for overview stripe)
#define WAVEFORM_POINTS 480

// VBR seek table resolution (1% increments, 0-99%)
#define SEEK_POINTS     100

// Maximum hot cues (matches Pioneer/Denon standard)
#define NUM_HOTCUES     8

// Ensure no padding bytes are inserted by the compiler
// This guarantees identical binary layout on ESP32 and PC
#pragma pack(push, 1)

/**
 * @brief Hot cue point structure
 * 
 * Stores a single cue point or loop marker with position, color, and type.
 * Size: 10 bytes
 */
typedef struct {
    uint32_t position_ms;    ///< Position in milliseconds from track start
    uint16_t color_rgb565;   ///< Display color (RGB565: Green=0x07E0 for loops, Red=0xF800 for cues)
    uint8_t  active;         ///< 0=Empty slot, 1=Cue point set
    uint8_t  is_loop;        ///< 0=Cue point, 1=Loop start marker
    uint8_t  reserved[2];    ///< Future use (e.g., loop end offset)
} HotCue_t;

/**
 * @brief Track metadata structure
 * 
 * Complete pre-analyzed track data for instant loading.
 * Total size: ~984 bytes
 * 
 * Layout:
 * - Header:      12 bytes (magic, version, source_size)
 * - Audio:       12 bytes (duration, BPM, key, grid)
 * - Navigation: 400 bytes (seek_table[100])
 * - Visuals:    480 bytes (waveform_overview[480])
 * - Performance: 80 bytes (hotcues[8])
 */
typedef struct {
    // -- Header (12 bytes) --
    uint32_t magic;          ///< Must be ODK_MAGIC (0x4F444B31)
    uint32_t version;        ///< Must be ODK_VERSION (1)
    uint32_t source_size;    ///< Original MP3 file size (detect if file changed)

    // -- Audio Properties (12 bytes) --
    uint32_t duration_ms;    ///< Track duration in milliseconds
    float    bpm;            ///< Beats per minute (e.g., 128.00)
    uint8_t  key_id;         ///< Musical key in Camelot notation (0-23: 1A-12B)
    uint8_t  grid_offset;    ///< First downbeat offset in milliseconds
    uint8_t  reserved[2];    ///< Future use

    // -- Navigation (400 bytes) --
    // Byte offset in MP3 file for 0%, 1%, 2%... 99% of track
    // Critical for accurate VBR seeking
    uint32_t seek_table[SEEK_POINTS];

    // -- Visuals (480 bytes) --
    // Normalized amplitude peaks (0-255) for the overview stripe
    // Used for navigation bar at bottom of screen
    uint8_t  waveform_overview[WAVEFORM_POINTS];

    // -- Performance (80 bytes) --
    // Hot cue points and loop markers
    HotCue_t hotcues[NUM_HOTCUES];

} TrackMetadata_t;

#pragma pack(pop)

// Compile-time size verification
_Static_assert(sizeof(HotCue_t) == 10, "HotCue_t must be 10 bytes");
_Static_assert(sizeof(TrackMetadata_t) == 984, "TrackMetadata_t must be 984 bytes");

/**
 * @brief Camelot key names lookup table
 * 
 * Index 0-11: Minor keys (1A-12A)
 * Index 12-23: Major keys (1B-12B)
 */
static const char* const CAMELOT_KEYS[] = {
    "1A", "2A", "3A", "4A", "5A", "6A", "7A", "8A", "9A", "10A", "11A", "12A",
    "1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B", "10B", "11B", "12B"
};

#ifdef __cplusplus
}
#endif
