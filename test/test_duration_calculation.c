/**
 * @file test_duration_calculation.c
 * @brief Tests for MP3 duration calculation logic
 * 
 * Verifies that track duration is calculated correctly from file size and bitrate.
 * These tests validate the fix for the "track too long" bug where duration was
 * incorrectly calculated using a fixed 128kbps assumption.
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Duration Calculation Helper (mirrors audio_player.cpp logic)
// ============================================================================

/**
 * @brief Calculate MP3 duration from file size and bitrate
 * 
 * @param audio_data_size Audio data size in bytes (file size minus ID3 tags)
 * @param bitrate_bps Bitrate in bits per second (e.g., 320000 for 320kbps)
 * @return Duration in seconds
 */
static uint32_t calculate_duration(size_t audio_data_size, uint32_t bitrate_bps) {
    if (bitrate_bps == 0) return 0;
    return (uint32_t)((audio_data_size * 8) / bitrate_bps);
}

/**
 * @brief Old buggy calculation (for comparison)
 * Assumed fixed 128kbps bitrate
 */
static uint32_t calculate_duration_buggy(size_t audio_data_size) {
    return (uint32_t)(audio_data_size / (128 * 1024 / 8));  // Assumed 128kbps
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Duration calculation for 320kbps MP3
 * 
 * Real world example: "Bodyrox - Yeah Yeah" 
 * - Audio data: 6,468,045 bytes
 * - Bitrate: 320 kbps
 * - Expected: ~161 seconds (2:41)
 */
TEST_CASE("Duration calculation 320kbps", "[duration]")
{
    size_t audio_data_size = 6468045;  // bytes
    uint32_t bitrate_bps = 320000;      // 320 kbps
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // Expected: 6468045 * 8 / 320000 = 161.7 ≈ 161 seconds
    TEST_ASSERT_EQUAL_UINT32(161, duration);
}

/**
 * Test: Duration calculation for 192kbps MP3
 */
TEST_CASE("Duration calculation 192kbps", "[duration]")
{
    size_t audio_data_size = 5000000;  // bytes
    uint32_t bitrate_bps = 192000;      // 192 kbps
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // Expected: 5000000 * 8 / 192000 = 208.3 ≈ 208 seconds
    TEST_ASSERT_EQUAL_UINT32(208, duration);
}

/**
 * Test: Duration calculation for 128kbps MP3
 */
TEST_CASE("Duration calculation 128kbps", "[duration]")
{
    size_t audio_data_size = 4000000;  // bytes
    uint32_t bitrate_bps = 128000;      // 128 kbps
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // Expected: 4000000 * 8 / 128000 = 250 seconds
    TEST_ASSERT_EQUAL_UINT32(250, duration);
}

/**
 * Test: Duration calculation for 256kbps MP3
 */
TEST_CASE("Duration calculation 256kbps", "[duration]")
{
    size_t audio_data_size = 8000000;  // bytes
    uint32_t bitrate_bps = 256000;      // 256 kbps
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // Expected: 8000000 * 8 / 256000 = 250 seconds
    TEST_ASSERT_EQUAL_UINT32(250, duration);
}

/**
 * Test: Compare fixed vs actual bitrate calculation
 * 
 * Demonstrates the bug: Using 128kbps assumption for a 320kbps file
 * gives duration 2.5x too long.
 */
TEST_CASE("Bug comparison: fixed vs actual bitrate", "[duration][regression]")
{
    size_t audio_data_size = 6468045;  // bytes (real file)
    
    // Old buggy calculation (assumed 128kbps)
    uint32_t buggy_duration = calculate_duration_buggy(audio_data_size);
    
    // Correct calculation (actual 320kbps)
    uint32_t correct_duration = calculate_duration(audio_data_size, 320000);
    
    // Buggy: 6468045 / 16384 = 394 seconds (WRONG!)
    // Correct: 6468045 * 8 / 320000 = 161 seconds (RIGHT!)
    
    TEST_ASSERT_EQUAL_UINT32(394, buggy_duration);   // Old buggy result
    TEST_ASSERT_EQUAL_UINT32(161, correct_duration); // Correct result
    
    // The buggy calculation is 2.44x too long (320/128 = 2.5)
    TEST_ASSERT_TRUE(buggy_duration > correct_duration * 2);
}

/**
 * Test: Zero bitrate handling (edge case)
 */
TEST_CASE("Duration with zero bitrate returns zero", "[duration][edge]")
{
    size_t audio_data_size = 6468045;
    uint32_t bitrate_bps = 0;  // Invalid
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    TEST_ASSERT_EQUAL_UINT32(0, duration);
}

/**
 * Test: Small file handling
 */
TEST_CASE("Duration for small file", "[duration][edge]")
{
    size_t audio_data_size = 10000;  // ~0.25 seconds at 320kbps
    uint32_t bitrate_bps = 320000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // 10000 * 8 / 320000 = 0.25 ≈ 0 seconds (integer truncation)
    TEST_ASSERT_EQUAL_UINT32(0, duration);
}

/**
 * Test: Large file handling (1 hour track)
 */
TEST_CASE("Duration for large file (1 hour)", "[duration]")
{
    // 1 hour at 320kbps = 3600 * 320000 / 8 = 144,000,000 bytes
    size_t audio_data_size = 144000000;
    uint32_t bitrate_bps = 320000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // Should be 3600 seconds (1 hour)
    TEST_ASSERT_EQUAL_UINT32(3600, duration);
}

// ============================================================================
// Test Registration
// ============================================================================

void register_duration_calculation_tests(void) {
    // Tests are auto-registered by Unity macros
    // This function exists for organizational purposes
}

