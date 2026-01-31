/**
 * @file test_audio_player.c
 * @brief Tests for audio player functionality
 * 
 * Tests pure logic functions that don't require hardware.
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Position Calculation
// ============================================================================

static uint32_t calculate_position_seconds(uint64_t bytes_played, uint32_t sample_rate, int channels) {
    if (sample_rate == 0 || channels == 0) return 0;
    uint64_t samples = bytes_played / (2 * channels);
    return (uint32_t)(samples / sample_rate);
}

void test_position_1_second(void) {
    // 1 second of stereo 44.1kHz 16-bit audio = 176400 bytes
    uint64_t bytes = 176400;
    uint32_t pos = calculate_position_seconds(bytes, 44100, 2);
    printf("  1 second (176400 bytes): pos=%lu (expected 1)\n", (unsigned long)pos);
    TEST_ASSERT_EQUAL_UINT32(1, pos);
}

void test_position_60_seconds(void) {
    uint64_t bytes = 60ULL * 44100 * 2 * 2;
    uint32_t pos = calculate_position_seconds(bytes, 44100, 2);
    printf("  60 seconds: pos=%lu (expected 60)\n", (unsigned long)pos);
    TEST_ASSERT_EQUAL_UINT32(60, pos);
}

void test_position_zero_sample_rate(void) {
    uint32_t pos = calculate_position_seconds(176400, 0, 2);
    printf("  Zero sample rate: pos=%lu (expected 0)\n", (unsigned long)pos);
    TEST_ASSERT_EQUAL_UINT32(0, pos);
}

// ============================================================================
// Gain/Volume
// ============================================================================

static int16_t apply_gain(int16_t sample, float gain) {
    int32_t result = (int32_t)(sample * gain);
    if (result > 32767) result = 32767;
    if (result < -32768) result = -32768;
    return (int16_t)result;
}

void test_gain_unity(void) {
    int16_t result = apply_gain(16384, 1.0f);
    printf("  Gain 1.0: %d -> %d (expected 16384)\n", 16384, result);
    TEST_ASSERT_EQUAL_INT16(16384, result);
}

void test_gain_half(void) {
    int16_t result = apply_gain(16384, 0.5f);
    printf("  Gain 0.5: %d -> %d (expected 8192)\n", 16384, result);
    TEST_ASSERT_EQUAL_INT16(8192, result);
}

void test_gain_mute(void) {
    int16_t result = apply_gain(16384, 0.0f);
    printf("  Gain 0.0: %d -> %d (expected 0)\n", 16384, result);
    TEST_ASSERT_EQUAL_INT16(0, result);
}

void test_gain_clamp_positive(void) {
    int16_t result = apply_gain(32767, 2.0f);
    printf("  Gain 2.0 (overflow): 32767 -> %d (expected 32767)\n", result);
    TEST_ASSERT_EQUAL_INT16(32767, result);
}

void test_gain_clamp_negative(void) {
    int16_t result = apply_gain(-32768, 2.0f);
    printf("  Gain 2.0 (underflow): -32768 -> %d (expected -32768)\n", result);
    TEST_ASSERT_EQUAL_INT16(-32768, result);
}

// ============================================================================
// Ring Buffer
// ============================================================================

#define TEST_RING_SIZE 256

typedef struct {
    uint8_t buffer[TEST_RING_SIZE];
    size_t write_head;
    size_t read_head;
    size_t available;
} test_ring_buffer_t;

static void ring_init(test_ring_buffer_t *rb) {
    memset(rb->buffer, 0, TEST_RING_SIZE);
    rb->write_head = 0;
    rb->read_head = 0;
    rb->available = 0;
}

static size_t ring_write(test_ring_buffer_t *rb, const uint8_t *data, size_t len) {
    size_t written = 0;
    while (written < len && rb->available < TEST_RING_SIZE) {
        rb->buffer[rb->write_head] = data[written];
        rb->write_head = (rb->write_head + 1) % TEST_RING_SIZE;
        rb->available++;
        written++;
    }
    return written;
}

static size_t ring_read(test_ring_buffer_t *rb, uint8_t *data, size_t len) {
    size_t read_count = 0;
    while (read_count < len && rb->available > 0) {
        data[read_count] = rb->buffer[rb->read_head];
        rb->read_head = (rb->read_head + 1) % TEST_RING_SIZE;
        rb->available--;
        read_count++;
    }
    return read_count;
}

void test_ring_buffer_basic(void) {
    test_ring_buffer_t rb;
    ring_init(&rb);
    
    uint8_t write_data[] = {1, 2, 3, 4, 5};
    uint8_t read_data[5] = {0};
    
    size_t written = ring_write(&rb, write_data, 5);
    size_t read_count = ring_read(&rb, read_data, 5);
    
    printf("  Basic: wrote=%zu, read=%zu\n", written, read_count);
    TEST_ASSERT_EQUAL_UINT(5, written);
    TEST_ASSERT_EQUAL_UINT(5, read_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data, read_data, 5);
}

void test_ring_buffer_wrap(void) {
    test_ring_buffer_t rb;
    ring_init(&rb);
    
    // Fill most of buffer
    uint8_t fill[TEST_RING_SIZE - 10];
    memset(fill, 0xAA, sizeof(fill));
    ring_write(&rb, fill, sizeof(fill));
    
    // Read most
    uint8_t discard[TEST_RING_SIZE - 10];
    ring_read(&rb, discard, sizeof(discard));
    
    // Write data that wraps
    uint8_t wrap_data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    ring_write(&rb, wrap_data, 15);
    
    // Read back
    uint8_t read_data[15] = {0};
    ring_read(&rb, read_data, 15);
    
    printf("  Wrap around: data matches=%d\n", 
           memcmp(wrap_data, read_data, 15) == 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wrap_data, read_data, 15);
}

void test_ring_buffer_full(void) {
    test_ring_buffer_t rb;
    ring_init(&rb);
    
    uint8_t data[TEST_RING_SIZE + 50];
    memset(data, 0x55, sizeof(data));
    
    size_t written = ring_write(&rb, data, sizeof(data));
    
    printf("  Full condition: wrote=%zu (expected %d)\n", written, TEST_RING_SIZE);
    TEST_ASSERT_EQUAL_UINT(TEST_RING_SIZE, written);
    TEST_ASSERT_EQUAL_UINT(TEST_RING_SIZE, rb.available);
}

// ============================================================================
// Test Registration
// ============================================================================

void register_audio_player_tests(void) {
    printf("\n--- Audio Player Tests ---\n");
    
    printf("\n[Position Calculation]\n");
    RUN_TEST(test_position_1_second);
    RUN_TEST(test_position_60_seconds);
    RUN_TEST(test_position_zero_sample_rate);
    
    printf("\n[Gain/Volume]\n");
    RUN_TEST(test_gain_unity);
    RUN_TEST(test_gain_half);
    RUN_TEST(test_gain_mute);
    RUN_TEST(test_gain_clamp_positive);
    RUN_TEST(test_gain_clamp_negative);
    
    printf("\n[Ring Buffer]\n");
    RUN_TEST(test_ring_buffer_basic);
    RUN_TEST(test_ring_buffer_wrap);
    RUN_TEST(test_ring_buffer_full);
}
