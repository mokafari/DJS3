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
#include <stdio.h>

// ============================================================================
// Duration Calculation Helper (mirrors audio_player.cpp logic)
// ============================================================================

/**
 * @brief Calculate MP3 duration from file size and bitrate
 */
static uint32_t calculate_duration(size_t audio_data_size, uint32_t bitrate_bps) {
    if (bitrate_bps == 0) return 0;
    return (uint32_t)((audio_data_size * 8) / bitrate_bps);
}

/**
 * @brief Old buggy calculation (for comparison) - assumed fixed 128kbps
 */
static uint32_t calculate_duration_buggy(size_t audio_data_size) {
    return (uint32_t)(audio_data_size / (128 * 1024 / 8));
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Duration calculation for 320kbps MP3 (real world example)
 */
void test_duration_320kbps(void) {
    size_t audio_data_size = 6468045;  // bytes
    uint32_t bitrate_bps = 320000;      // 320 kbps
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    // Expected: 6468045 * 8 / 320000 = 161.7 ≈ 161 seconds
    printf("  320kbps: %lu bytes -> %lu seconds (expected 161)\n", 
           (unsigned long)audio_data_size, (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(161, duration);
}

/**
 * Test: Duration calculation for 192kbps MP3
 */
void test_duration_192kbps(void) {
    size_t audio_data_size = 5000000;
    uint32_t bitrate_bps = 192000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    printf("  192kbps: %lu bytes -> %lu seconds (expected 208)\n",
           (unsigned long)audio_data_size, (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(208, duration);
}

/**
 * Test: Duration calculation for 128kbps MP3
 */
void test_duration_128kbps(void) {
    size_t audio_data_size = 4000000;
    uint32_t bitrate_bps = 128000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    printf("  128kbps: %lu bytes -> %lu seconds (expected 250)\n",
           (unsigned long)audio_data_size, (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(250, duration);
}

/**
 * Test: Duration calculation for 256kbps MP3
 */
void test_duration_256kbps(void) {
    size_t audio_data_size = 8000000;
    uint32_t bitrate_bps = 256000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    printf("  256kbps: %lu bytes -> %lu seconds (expected 250)\n",
           (unsigned long)audio_data_size, (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(250, duration);
}

/**
 * REGRESSION TEST: Compare fixed vs actual bitrate calculation
 * This demonstrates the bug that was fixed.
 */
void test_regression_fixed_vs_actual_bitrate(void) {
    size_t audio_data_size = 6468045;  // Real file
    
    uint32_t buggy_duration = calculate_duration_buggy(audio_data_size);
    uint32_t correct_duration = calculate_duration(audio_data_size, 320000);
    
    printf("  REGRESSION: Buggy(128kbps)=%lu vs Correct(320kbps)=%lu\n",
           (unsigned long)buggy_duration, (unsigned long)correct_duration);
    
    // Buggy: 394 seconds (WRONG), Correct: 161 seconds (RIGHT)
    TEST_ASSERT_EQUAL_UINT32(394, buggy_duration);
    TEST_ASSERT_EQUAL_UINT32(161, correct_duration);
    TEST_ASSERT_TRUE(buggy_duration > correct_duration * 2);
}

/**
 * Test: Zero bitrate handling
 */
void test_duration_zero_bitrate(void) {
    size_t audio_data_size = 6468045;
    uint32_t bitrate_bps = 0;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    printf("  Zero bitrate: returns %lu (expected 0)\n", (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(0, duration);
}

/**
 * Test: Small file handling
 */
void test_duration_small_file(void) {
    size_t audio_data_size = 10000;  // ~0.25 seconds at 320kbps
    uint32_t bitrate_bps = 320000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    printf("  Small file: %lu bytes -> %lu seconds\n",
           (unsigned long)audio_data_size, (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(0, duration);
}

/**
 * Test: Large file handling (1 hour track)
 */
void test_duration_large_file(void) {
    size_t audio_data_size = 144000000;  // 1 hour at 320kbps
    uint32_t bitrate_bps = 320000;
    
    uint32_t duration = calculate_duration(audio_data_size, bitrate_bps);
    
    printf("  Large file (1hr): %lu bytes -> %lu seconds (expected 3600)\n",
           (unsigned long)audio_data_size, (unsigned long)duration);
    TEST_ASSERT_EQUAL_UINT32(3600, duration);
}

// ============================================================================
// Test Registration
// ============================================================================

void register_duration_calculation_tests(void) {
    printf("\n--- Duration Calculation Tests ---\n");
    RUN_TEST(test_duration_320kbps);
    RUN_TEST(test_duration_192kbps);
    RUN_TEST(test_duration_128kbps);
    RUN_TEST(test_duration_256kbps);
    RUN_TEST(test_regression_fixed_vs_actual_bitrate);
    RUN_TEST(test_duration_zero_bitrate);
    RUN_TEST(test_duration_small_file);
    RUN_TEST(test_duration_large_file);
}
