#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Key detection result
typedef struct {
    uint8_t key_id;         // Camelot key (0-23), 255 if unknown
    float confidence;       // Confidence score (0.0 - 1.0)
    const char *key_name;   // Human-readable ("8A", "5B", etc.)
} key_result_t;

// Detect key from PCM audio
// samples: interleaved stereo 16-bit PCM at 44100 Hz
// num_samples: total sample count
key_result_t key_detect(const int16_t *samples, size_t num_samples);

// Get key name from ID
const char* key_get_name(uint8_t key_id);

// Get compatible keys for harmonic mixing (Camelot wheel neighbors)
// Returns count of compatible keys (up to 6: same, +1, -1, relative major/minor)
int key_get_compatible(uint8_t key_id, uint8_t *compatible, size_t max_count);
