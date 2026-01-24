/**
 * @file slip_loop_example.c
 * @brief Example usage of slip loop engine
 * 
 * This demonstrates how to use the slip loop engine for short stutters
 * with both time-based and beat-synced modes.
 */

#include "slip_loop.h"
#include <stdio.h>

void example_slip_loop_usage(void) {
    slip_loop_t slip;
    int16_t audio_buffer[44100 * 10]; // 10 seconds of audio
    int16_t output_buffer[64 * 2];    // 64 samples stereo
    
    // Initialize slip loop with 4 second buffer
    slip_loop_init(&slip, 44100 * 4, 44100);
    
    // Set BPM for beat-synced mode
    slip_loop_set_bpm(&slip, 120.0f);
    
    // Example 1: Time-based slip loop (500ms stutter)
    uint32_t current_pos = 100000; // Current playback position
    slip_loop_start_time(&slip, current_pos, 500); // 500ms loop
    
    // Process audio - will loop the 500ms section
    while (slip_loop_is_active(&slip)) {
        slip_loop_process(&slip, audio_buffer, sizeof(audio_buffer) / 2, 
                         output_buffer, 64);
        // Output audio...
    }
    
    // Stop loop and get background position
    uint32_t background_pos = slip_loop_stop(&slip);
    // Jump playback to background_pos to continue from where track would be
    
    // Example 2: Beat-synced slip loop (1 beat stutter)
    current_pos = 200000;
    slip_loop_start_beat(&slip, current_pos, 1); // 1 beat loop
    
    // Process audio - will loop 1 beat
    while (slip_loop_is_active(&slip)) {
        slip_loop_process(&slip, audio_buffer, sizeof(audio_buffer) / 2,
                         output_buffer, 64);
        // Output audio...
        
        // Get loop position for UI feedback
        float loop_pos = slip_loop_get_position(&slip);
        // Display loop position (0.0 to 1.0)
    }
    
    background_pos = slip_loop_stop(&slip);
    
    // Cleanup
    slip_loop_deinit(&slip);
}

/**
 * @brief Integration example with main audio player
 */
void example_integration_with_player(void) {
    slip_loop_t slip;
    int16_t *main_audio_buffer; // Your main audio buffer
    size_t main_buffer_size;
    uint32_t current_playback_pos = 0;
    
    // Initialize
    slip_loop_init(&slip, 44100 * 4, 44100);
    
    // In your audio processing loop:
    while (1) {
        // Process through slip loop
        int16_t output[64 * 2];
        slip_loop_process(&slip, main_audio_buffer, main_buffer_size,
                         output, 64);
        
        // Update playback position (only if not in slip mode)
        if (!slip_loop_is_active(&slip)) {
            current_playback_pos += 64;
            if (current_playback_pos >= main_buffer_size) {
                current_playback_pos = 0;
            }
        }
        
        // When user presses "slip loop" button:
        // slip_loop_start_time(&slip, current_playback_pos, 500);
        
        // When user releases "slip loop" button:
        // current_playback_pos = slip_loop_stop(&slip);
    }
}

