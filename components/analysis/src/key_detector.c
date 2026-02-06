#include "key_detector.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Camelot key names
// Index 0-11 = minor (A), 12-23 = major (B)
static const char* KEY_NAMES[] = {
    "1A", "2A", "3A", "4A", "5A", "6A", "7A", "8A", "9A", "10A", "11A", "12A",
    "1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B", "10B", "11B", "12B"
};

// Krumhansl-Kessler major key profile (normalized)
// These represent the hierarchical importance of each pitch class in a major key
static const float MAJOR_PROFILE[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

// Krumhansl-Kessler minor key profile (normalized)
static const float MINOR_PROFILE[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

// Goertzel algorithm - efficient single-frequency DFT
// Returns magnitude squared for the target frequency
static float goertzel(const int16_t *samples, size_t count, float freq, float sample_rate) {
    float omega = 2.0f * M_PI * freq / sample_rate;
    float coeff = 2.0f * cosf(omega);
    float s0 = 0, s1 = 0, s2 = 0;
    
    for (size_t i = 0; i < count; i++) {
        s0 = samples[i] / 32768.0f + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Extract chroma features (12-element array representing pitch class energies)
static void extract_chroma(const int16_t *samples, size_t num_samples, 
                          float sample_rate, float chroma[12]) {
    // Note frequencies for octave 4 (middle octave)
    static const float NOTE_FREQS[12] = {
        261.63f,  // C4
        277.18f,  // C#4
        293.66f,  // D4
        311.13f,  // D#4
        329.63f,  // E4
        349.23f,  // F4
        369.99f,  // F#4
        392.00f,  // G4
        415.30f,  // G#4
        440.00f,  // A4
        466.16f,  // A#4
        493.88f   // B4
    };
    
    memset(chroma, 0, 12 * sizeof(float));
    
    // Process audio in chunks
    size_t chunk_size = 4096;
    int16_t *mono = malloc(chunk_size * sizeof(int16_t));
    if (!mono) return;
    
    size_t chunks_processed = 0;
    
    for (size_t offset = 0; offset + chunk_size * 2 <= num_samples; offset += chunk_size * 2) {
        // Convert stereo to mono
        for (size_t i = 0; i < chunk_size; i++) {
            mono[i] = (samples[offset + i*2] + samples[offset + i*2 + 1]) / 2;
        }
        
        // Accumulate energy for each pitch class across multiple octaves
        for (int note = 0; note < 12; note++) {
            // Check octaves 2-5 (65 Hz to 988 Hz range)
            for (int octave = 2; octave <= 5; octave++) {
                float freq = NOTE_FREQS[note] * powf(2.0f, octave - 4);
                chroma[note] += goertzel(mono, chunk_size, freq, sample_rate);
            }
        }
        chunks_processed++;
        
        // Limit processing for very long tracks
        if (chunks_processed >= 100) break;
    }
    
    free(mono);
    
    // Normalize chroma vector to [0, 1]
    float max_val = 0;
    for (int i = 0; i < 12; i++) {
        if (chroma[i] > max_val) max_val = chroma[i];
    }
    if (max_val > 0) {
        for (int i = 0; i < 12; i++) {
            chroma[i] /= max_val;
        }
    }
}

// Correlate chroma vector with key profile (rotated to different root notes)
static float correlate(const float chroma[12], const float profile[12], int rotation) {
    float sum = 0;
    for (int i = 0; i < 12; i++) {
        int j = (i + rotation) % 12;
        sum += chroma[i] * profile[j];
    }
    return sum;
}

// Convert pitch class (0=C) to Camelot key ID
// Camelot wheel mapping:
// Minor (A): 1A=Abm 2A=Ebm 3A=Bbm 4A=Fm 5A=Cm 6A=Gm 7A=Dm 8A=Am 9A=Em 10A=Bm 11A=F#m 12A=C#m
// Major (B): 1B=B   2B=F#  3B=Db  4B=Ab 5B=Eb 6B=Bb 7B=F  8B=C  9B=G  10B=D  11B=A  12B=E
static uint8_t pitch_to_camelot(int pitch_class, bool is_major) {
    // Pitch class to Camelot number mapping
    // Pitch: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F, 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B
    // For major: C=8B, G=9B, D=10B, A=11B, E=12B, B=1B, F#=2B, Db=3B, Ab=4B, Eb=5B, Bb=6B, F=7B
    // For minor: Same number but A instead of B
    static const int PITCH_TO_CAMELOT[12] = {
        8,  // C  -> 8
        3,  // C# -> 3 (Db)
        10, // D  -> 10
        5,  // D# -> 5 (Eb)
        12, // E  -> 12
        7,  // F  -> 7
        2,  // F# -> 2
        9,  // G  -> 9
        4,  // G# -> 4 (Ab)
        11, // A  -> 11
        6,  // A# -> 6 (Bb)
        1   // B  -> 1
    };
    
    int camelot_num = PITCH_TO_CAMELOT[pitch_class];
    // Camelot 1-12, convert to 0-11 for A (minor), 12-23 for B (major)
    return is_major ? (camelot_num - 1 + 12) : (camelot_num - 1);
}

key_result_t key_detect(const int16_t *samples, size_t num_samples) {
    key_result_t result = {255, 0.0f, "?"};
    
    if (!samples || num_samples < 8192) return result;
    
    float chroma[12];
    extract_chroma(samples, num_samples, 44100.0f, chroma);
    
    float best_corr = -1;
    int best_pitch = 0;
    bool best_is_major = false;
    
    // Try all 12 pitch classes for both major and minor profiles
    for (int pitch = 0; pitch < 12; pitch++) {
        float major_corr = correlate(chroma, MAJOR_PROFILE, pitch);
        float minor_corr = correlate(chroma, MINOR_PROFILE, pitch);
        
        if (major_corr > best_corr) {
            best_corr = major_corr;
            best_pitch = pitch;
            best_is_major = true;
        }
        if (minor_corr > best_corr) {
            best_corr = minor_corr;
            best_pitch = pitch;
            best_is_major = false;
        }
    }
    
    result.key_id = pitch_to_camelot(best_pitch, best_is_major);
    // Normalize correlation to approximate confidence (0-1)
    result.confidence = best_corr / 50.0f;
    if (result.confidence > 1.0f) result.confidence = 1.0f;
    result.key_name = KEY_NAMES[result.key_id];
    
    return result;
}

const char* key_get_name(uint8_t key_id) {
    if (key_id >= 24) return "?";
    return KEY_NAMES[key_id];
}

int key_get_compatible(uint8_t key_id, uint8_t *compatible, size_t max_count) {
    if (key_id >= 24 || !compatible || max_count < 1) return 0;
    
    int count = 0;
    bool is_major = key_id >= 12;
    int camelot_num = (key_id % 12) + 1;  // 1-12
    
    // Same key (perfect match)
    if (count < (int)max_count) compatible[count++] = key_id;
    
    // +1 on Camelot wheel (energy boost)
    int plus1 = (camelot_num % 12) + 1;  // 1-12 wrapping
    if (count < (int)max_count) compatible[count++] = (plus1 - 1) + (is_major ? 12 : 0);
    
    // -1 on Camelot wheel (energy drop)
    int minus1 = ((camelot_num - 2 + 12) % 12) + 1;
    if (count < (int)max_count) compatible[count++] = (minus1 - 1) + (is_major ? 12 : 0);
    
    // Relative major/minor (same number, different letter)
    if (count < (int)max_count) compatible[count++] = (key_id + 12) % 24;
    
    return count;
}
