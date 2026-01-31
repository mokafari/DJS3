/**
 * @file test_waveform_view.c
 * @brief Tests for waveform display logic
 * 
 * Tests the waveform rendering calculations without requiring actual display hardware.
 * Validates resolution divider, binned peak calculation, and scroll delta logic.
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ============================================================================
// Waveform Calculation Helpers (mirrors waveform_view.c logic)
// ============================================================================

#define WAVEFORM_BARS 480
#define SCROLL_DELTA_THRESHOLD 30

/**
 * @brief Get binned peak value (MAX of samples in bin)
 * 
 * Used to preserve transients when reducing resolution.
 */
static uint8_t get_binned_peak(const uint8_t *data, int start_idx, int bin_size, int max_idx) {
    uint8_t max_peak = 0;
    for (int i = 0; i < bin_size && (start_idx + i) < max_idx; i++) {
        if (data[start_idx + i] > max_peak) {
            max_peak = data[start_idx + i];
        }
    }
    return max_peak;
}

/**
 * @brief Calculate effective bars based on resolution divider
 */
static int calculate_effective_bars(int resolution_divider) {
    return WAVEFORM_BARS / resolution_divider;
}

/**
 * @brief Determine if full redraw is needed based on scroll delta
 */
static bool needs_full_redraw(int scroll_delta, bool first_frame) {
    if (first_frame) return true;
    if (scroll_delta < 0) return true;  // Backward scroll (scrubbing)
    if (scroll_delta > SCROLL_DELTA_THRESHOLD) return true;  // Large jump (seek)
    return false;
}

// ============================================================================
// Test Cases: Resolution Divider
// ============================================================================

/**
 * Test: Resolution divider 1 (full resolution)
 */
TEST_CASE("Resolution divider 1 gives 480 bars", "[waveform][resolution]")
{
    int effective_bars = calculate_effective_bars(1);
    TEST_ASSERT_EQUAL_INT(480, effective_bars);
}

/**
 * Test: Resolution divider 2 (half resolution)
 */
TEST_CASE("Resolution divider 2 gives 240 bars", "[waveform][resolution]")
{
    int effective_bars = calculate_effective_bars(2);
    TEST_ASSERT_EQUAL_INT(240, effective_bars);
}

/**
 * Test: Resolution divider 4 (quarter resolution)
 */
TEST_CASE("Resolution divider 4 gives 120 bars", "[waveform][resolution]")
{
    int effective_bars = calculate_effective_bars(4);
    TEST_ASSERT_EQUAL_INT(120, effective_bars);
}

/**
 * Test: Resolution divider 8 (eighth resolution)
 */
TEST_CASE("Resolution divider 8 gives 60 bars", "[waveform][resolution]")
{
    int effective_bars = calculate_effective_bars(8);
    TEST_ASSERT_EQUAL_INT(60, effective_bars);
}

// ============================================================================
// Test Cases: Binned Peak Calculation
// ============================================================================

/**
 * Test: Binned peak returns max value in bin
 */
TEST_CASE("Binned peak returns maximum value", "[waveform][peak]")
{
    uint8_t data[] = {10, 50, 30, 80, 20, 15, 90, 25};
    
    // Bin size 4, starting at 0: should find max of {10, 50, 30, 80} = 80
    uint8_t peak = get_binned_peak(data, 0, 4, 8);
    TEST_ASSERT_EQUAL_UINT8(80, peak);
}

/**
 * Test: Binned peak with offset
 */
TEST_CASE("Binned peak with offset", "[waveform][peak]")
{
    uint8_t data[] = {10, 50, 30, 80, 20, 15, 90, 25};
    
    // Bin size 4, starting at 4: should find max of {20, 15, 90, 25} = 90
    uint8_t peak = get_binned_peak(data, 4, 4, 8);
    TEST_ASSERT_EQUAL_UINT8(90, peak);
}

/**
 * Test: Binned peak handles boundary
 */
TEST_CASE("Binned peak handles boundary correctly", "[waveform][peak][edge]")
{
    uint8_t data[] = {10, 50, 30};
    
    // Bin size 4 but only 3 elements: should find max of {10, 50, 30} = 50
    uint8_t peak = get_binned_peak(data, 0, 4, 3);
    TEST_ASSERT_EQUAL_UINT8(50, peak);
}

/**
 * Test: Binned peak with all zeros
 */
TEST_CASE("Binned peak with silence returns zero", "[waveform][peak][edge]")
{
    uint8_t data[] = {0, 0, 0, 0};
    
    uint8_t peak = get_binned_peak(data, 0, 4, 4);
    TEST_ASSERT_EQUAL_UINT8(0, peak);
}

/**
 * Test: Binned peak preserves transient
 */
TEST_CASE("Binned peak preserves transient spike", "[waveform][peak]")
{
    // Simulate audio with one loud transient
    uint8_t data[] = {5, 5, 5, 255, 5, 5, 5, 5};  // Transient at index 3
    
    // Bin covers the transient: should return 255
    uint8_t peak = get_binned_peak(data, 0, 8, 8);
    TEST_ASSERT_EQUAL_UINT8(255, peak);
}

// ============================================================================
// Test Cases: Scroll Delta Logic
// ============================================================================

/**
 * Test: First frame always needs full redraw
 */
TEST_CASE("First frame needs full redraw", "[waveform][scroll]")
{
    TEST_ASSERT_TRUE(needs_full_redraw(0, true));
    TEST_ASSERT_TRUE(needs_full_redraw(5, true));
    TEST_ASSERT_TRUE(needs_full_redraw(-5, true));
}

/**
 * Test: Backward scroll (scrubbing) needs full redraw
 */
TEST_CASE("Backward scroll needs full redraw", "[waveform][scroll]")
{
    TEST_ASSERT_TRUE(needs_full_redraw(-1, false));
    TEST_ASSERT_TRUE(needs_full_redraw(-10, false));
    TEST_ASSERT_TRUE(needs_full_redraw(-100, false));
}

/**
 * Test: Large forward jump (seek) needs full redraw
 */
TEST_CASE("Large forward jump needs full redraw", "[waveform][scroll]")
{
    TEST_ASSERT_TRUE(needs_full_redraw(31, false));   // Just over threshold
    TEST_ASSERT_TRUE(needs_full_redraw(100, false));  // Way over threshold
    TEST_ASSERT_TRUE(needs_full_redraw(1000, false)); // Huge jump
}

/**
 * Test: Small forward scroll uses incremental update
 */
TEST_CASE("Small forward scroll uses incremental", "[waveform][scroll]")
{
    TEST_ASSERT_FALSE(needs_full_redraw(1, false));
    TEST_ASSERT_FALSE(needs_full_redraw(5, false));
    TEST_ASSERT_FALSE(needs_full_redraw(10, false));
    TEST_ASSERT_FALSE(needs_full_redraw(29, false));  // Just under threshold
    TEST_ASSERT_FALSE(needs_full_redraw(30, false));  // At threshold (not over)
}

/**
 * Test: Zero scroll delta (paused) doesn't need redraw
 */
TEST_CASE("Zero scroll delta (paused) no redraw needed", "[waveform][scroll]")
{
    TEST_ASSERT_FALSE(needs_full_redraw(0, false));
}

// ============================================================================
// Test Cases: Display Cache Logic
// ============================================================================

/**
 * Test: Display cache shift for incremental scroll
 */
TEST_CASE("Display cache shifts correctly", "[waveform][cache]")
{
    uint8_t cache[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    
    // Simulate shift left by 2 positions
    int shift = 2;
    memmove(cache, cache + shift, 8 - shift);
    
    // First 6 positions should now be {3, 4, 5, 6, 7, 8}
    TEST_ASSERT_EQUAL_UINT8(3, cache[0]);
    TEST_ASSERT_EQUAL_UINT8(4, cache[1]);
    TEST_ASSERT_EQUAL_UINT8(5, cache[2]);
    TEST_ASSERT_EQUAL_UINT8(6, cache[3]);
    TEST_ASSERT_EQUAL_UINT8(7, cache[4]);
    TEST_ASSERT_EQUAL_UINT8(8, cache[5]);
}

// ============================================================================
// Test Cases: Bar Height Calculation
// ============================================================================

/**
 * @brief Calculate bar height from peak value
 */
static int calculate_bar_height(uint8_t peak, int waveform_height) {
    if (peak == 0) return 0;
    int bar_height = (peak * waveform_height) / 255;
    if (bar_height < 1) bar_height = 1;
    if (bar_height > waveform_height) bar_height = waveform_height;
    return bar_height;
}

/**
 * Test: Maximum peak gives maximum height
 */
TEST_CASE("Max peak gives max height", "[waveform][height]")
{
    int height = calculate_bar_height(255, 80);
    TEST_ASSERT_EQUAL_INT(80, height);
}

/**
 * Test: Zero peak gives zero height
 */
TEST_CASE("Zero peak gives zero height", "[waveform][height]")
{
    int height = calculate_bar_height(0, 80);
    TEST_ASSERT_EQUAL_INT(0, height);
}

/**
 * Test: Half peak gives half height
 */
TEST_CASE("Half peak gives approximately half height", "[waveform][height]")
{
    int height = calculate_bar_height(128, 80);
    // 128 * 80 / 255 = 40.1 ≈ 40
    TEST_ASSERT_INT_WITHIN(1, 40, height);
}

/**
 * Test: Minimum non-zero peak gives at least 1 pixel height
 */
TEST_CASE("Minimum peak gives at least 1 pixel", "[waveform][height][edge]")
{
    int height = calculate_bar_height(1, 80);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, height);
}

// ============================================================================
// Test Registration
// ============================================================================

void register_waveform_view_tests(void) {
    // Tests are auto-registered by Unity macros
    // This function exists for organizational purposes
}

