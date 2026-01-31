/**
 * @file test_waveform_view.c
 * @brief Tests for waveform display logic
 * 
 * Tests the waveform rendering calculations without requiring actual display hardware.
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Waveform Calculation Helpers (mirrors waveform_view.c logic)
// ============================================================================

#define WAVEFORM_BARS 480
#define SCROLL_DELTA_THRESHOLD 30

static uint8_t get_binned_peak(const uint8_t *data, int start_idx, int bin_size, int max_idx) {
    uint8_t max_peak = 0;
    for (int i = 0; i < bin_size && (start_idx + i) < max_idx; i++) {
        if (data[start_idx + i] > max_peak) {
            max_peak = data[start_idx + i];
        }
    }
    return max_peak;
}

static int calculate_effective_bars(int resolution_divider) {
    return WAVEFORM_BARS / resolution_divider;
}

static bool needs_full_redraw(int scroll_delta, bool first_frame) {
    if (first_frame) return true;
    if (scroll_delta < 0) return true;
    if (scroll_delta > SCROLL_DELTA_THRESHOLD) return true;
    return false;
}

static int calculate_bar_height(uint8_t peak, int waveform_height) {
    if (peak == 0) return 0;
    int bar_height = (peak * waveform_height) / 255;
    if (bar_height < 1) bar_height = 1;
    if (bar_height > waveform_height) bar_height = waveform_height;
    return bar_height;
}

// ============================================================================
// Resolution Tests
// ============================================================================

void test_resolution_divider_1(void) {
    int bars = calculate_effective_bars(1);
    printf("  Divider 1: %d bars (expected 480)\n", bars);
    TEST_ASSERT_EQUAL_INT(480, bars);
}

void test_resolution_divider_2(void) {
    int bars = calculate_effective_bars(2);
    printf("  Divider 2: %d bars (expected 240)\n", bars);
    TEST_ASSERT_EQUAL_INT(240, bars);
}

void test_resolution_divider_4(void) {
    int bars = calculate_effective_bars(4);
    printf("  Divider 4: %d bars (expected 120)\n", bars);
    TEST_ASSERT_EQUAL_INT(120, bars);
}

void test_resolution_divider_8(void) {
    int bars = calculate_effective_bars(8);
    printf("  Divider 8: %d bars (expected 60)\n", bars);
    TEST_ASSERT_EQUAL_INT(60, bars);
}

// ============================================================================
// Binned Peak Tests
// ============================================================================

void test_binned_peak_finds_max(void) {
    uint8_t data[] = {10, 50, 30, 80, 20, 15, 90, 25};
    uint8_t peak = get_binned_peak(data, 0, 4, 8);
    printf("  Bin [10,50,30,80]: peak=%d (expected 80)\n", peak);
    TEST_ASSERT_EQUAL_UINT8(80, peak);
}

void test_binned_peak_with_offset(void) {
    uint8_t data[] = {10, 50, 30, 80, 20, 15, 90, 25};
    uint8_t peak = get_binned_peak(data, 4, 4, 8);
    printf("  Bin [20,15,90,25]: peak=%d (expected 90)\n", peak);
    TEST_ASSERT_EQUAL_UINT8(90, peak);
}

void test_binned_peak_boundary(void) {
    uint8_t data[] = {10, 50, 30};
    uint8_t peak = get_binned_peak(data, 0, 4, 3);
    printf("  Bin at boundary: peak=%d (expected 50)\n", peak);
    TEST_ASSERT_EQUAL_UINT8(50, peak);
}

void test_binned_peak_silence(void) {
    uint8_t data[] = {0, 0, 0, 0};
    uint8_t peak = get_binned_peak(data, 0, 4, 4);
    printf("  Silence: peak=%d (expected 0)\n", peak);
    TEST_ASSERT_EQUAL_UINT8(0, peak);
}

void test_binned_peak_preserves_transient(void) {
    uint8_t data[] = {5, 5, 5, 255, 5, 5, 5, 5};
    uint8_t peak = get_binned_peak(data, 0, 8, 8);
    printf("  Transient [5,5,5,255,...]: peak=%d (expected 255)\n", peak);
    TEST_ASSERT_EQUAL_UINT8(255, peak);
}

// ============================================================================
// Scroll Delta Tests
// ============================================================================

void test_first_frame_needs_full_redraw(void) {
    bool result = needs_full_redraw(5, true);
    printf("  First frame: needs_full=%d (expected 1)\n", result);
    TEST_ASSERT_TRUE(result);
}

void test_backward_scroll_needs_full_redraw(void) {
    bool result = needs_full_redraw(-10, false);
    printf("  Backward scroll (-10): needs_full=%d (expected 1)\n", result);
    TEST_ASSERT_TRUE(result);
}

void test_large_jump_needs_full_redraw(void) {
    bool result = needs_full_redraw(100, false);
    printf("  Large jump (100): needs_full=%d (expected 1)\n", result);
    TEST_ASSERT_TRUE(result);
}

void test_small_scroll_incremental(void) {
    bool result = needs_full_redraw(5, false);
    printf("  Small scroll (5): needs_full=%d (expected 0)\n", result);
    TEST_ASSERT_FALSE(result);
}

void test_paused_no_redraw(void) {
    bool result = needs_full_redraw(0, false);
    printf("  Paused (0): needs_full=%d (expected 0)\n", result);
    TEST_ASSERT_FALSE(result);
}

// ============================================================================
// Bar Height Tests
// ============================================================================

void test_bar_height_max_peak(void) {
    int height = calculate_bar_height(255, 80);
    printf("  Max peak (255): height=%d (expected 80)\n", height);
    TEST_ASSERT_EQUAL_INT(80, height);
}

void test_bar_height_zero_peak(void) {
    int height = calculate_bar_height(0, 80);
    printf("  Zero peak: height=%d (expected 0)\n", height);
    TEST_ASSERT_EQUAL_INT(0, height);
}

void test_bar_height_half_peak(void) {
    int height = calculate_bar_height(128, 80);
    printf("  Half peak (128): height=%d (expected ~40)\n", height);
    TEST_ASSERT_INT_WITHIN(1, 40, height);
}

void test_bar_height_min_nonzero(void) {
    int height = calculate_bar_height(1, 80);
    printf("  Min nonzero (1): height=%d (expected >=1)\n", height);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, height);
}

// ============================================================================
// Test Registration
// ============================================================================

void register_waveform_view_tests(void) {
    printf("\n--- Waveform View Tests ---\n");
    
    printf("\n[Resolution Divider]\n");
    RUN_TEST(test_resolution_divider_1);
    RUN_TEST(test_resolution_divider_2);
    RUN_TEST(test_resolution_divider_4);
    RUN_TEST(test_resolution_divider_8);
    
    printf("\n[Binned Peak Detection]\n");
    RUN_TEST(test_binned_peak_finds_max);
    RUN_TEST(test_binned_peak_with_offset);
    RUN_TEST(test_binned_peak_boundary);
    RUN_TEST(test_binned_peak_silence);
    RUN_TEST(test_binned_peak_preserves_transient);
    
    printf("\n[Scroll Delta Logic]\n");
    RUN_TEST(test_first_frame_needs_full_redraw);
    RUN_TEST(test_backward_scroll_needs_full_redraw);
    RUN_TEST(test_large_jump_needs_full_redraw);
    RUN_TEST(test_small_scroll_incremental);
    RUN_TEST(test_paused_no_redraw);
    
    printf("\n[Bar Height Calculation]\n");
    RUN_TEST(test_bar_height_max_peak);
    RUN_TEST(test_bar_height_zero_peak);
    RUN_TEST(test_bar_height_half_peak);
    RUN_TEST(test_bar_height_min_nonzero);
}
