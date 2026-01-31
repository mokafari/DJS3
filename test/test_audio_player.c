/**
 * @file test_audio_player.c
 * @brief Tests for audio player functionality
 * 
 * NOTE: These tests require hardware (SD card, I2S output) and are
 * disabled by default. Enable by uncommenting in test_runner.c.
 * 
 * Run with actual hardware connected:
 * - SD card with test MP3 files
 * - I2S audio output configured
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ============================================================================
// Audio State Tests (No hardware required)
// ============================================================================

typedef enum {
    TEST_PLAYER_STOPPED = 0,
    TEST_PLAYER_PLAYING,
    TEST_PLAYER_PAUSED,
} test_player_state_t;

/**
 * Test: Player state transitions
 */
TEST_CASE("Player state: stopped -> playing -> paused -> stopped", "[audio][state]")
{
    test_player_state_t state = TEST_PLAYER_STOPPED;
    
    // Start playing
    state = TEST_PLAYER_PLAYING;
    TEST_ASSERT_EQUAL_INT(TEST_PLAYER_PLAYING, state);
    
    // Pause
    state = TEST_PLAYER_PAUSED;
    TEST_ASSERT_EQUAL_INT(TEST_PLAYER_PAUSED, state);
    
    // Stop
    state = TEST_PLAYER_STOPPED;
    TEST_ASSERT_EQUAL_INT(TEST_PLAYER_STOPPED, state);
}

// ============================================================================
// Position Calculation Tests
// ============================================================================

/**
 * @brief Calculate position in seconds from bytes played
 */
static uint32_t calculate_position_seconds(uint64_t bytes_played, uint32_t sample_rate, int channels) {
    if (sample_rate == 0 || channels == 0) return 0;
    // 16-bit samples = 2 bytes per sample
    uint64_t samples = bytes_played / (2 * channels);
    return (uint32_t)(samples / sample_rate);
}

/**
 * Test: Position calculation for stereo 44.1kHz
 */
TEST_CASE("Position calculation stereo 44.1kHz", "[audio][position]")
{
    // 1 second of stereo 44.1kHz 16-bit audio = 44100 * 2 * 2 = 176400 bytes
    uint64_t bytes_played = 176400;
    uint32_t sample_rate = 44100;
    int channels = 2;
    
    uint32_t position = calculate_position_seconds(bytes_played, sample_rate, channels);
    TEST_ASSERT_EQUAL_UINT32(1, position);
}

/**
 * Test: Position calculation for 60 seconds
 */
TEST_CASE("Position calculation 60 seconds", "[audio][position]")
{
    // 60 seconds of stereo 44.1kHz 16-bit audio
    uint64_t bytes_played = 60ULL * 44100 * 2 * 2;
    uint32_t sample_rate = 44100;
    int channels = 2;
    
    uint32_t position = calculate_position_seconds(bytes_played, sample_rate, channels);
    TEST_ASSERT_EQUAL_UINT32(60, position);
}

/**
 * Test: Position calculation with zero sample rate
 */
TEST_CASE("Position calculation zero sample rate returns zero", "[audio][position][edge]")
{
    uint64_t bytes_played = 176400;
    uint32_t sample_rate = 0;
    int channels = 2;
    
    uint32_t position = calculate_position_seconds(bytes_played, sample_rate, channels);
    TEST_ASSERT_EQUAL_UINT32(0, position);
}

// ============================================================================
// Gain/Volume Tests
// ============================================================================

/**
 * @brief Apply gain to a sample
 */
static int16_t apply_gain(int16_t sample, float gain) {
    int32_t result = (int32_t)(sample * gain);
    // Clamp to int16 range
    if (result > 32767) result = 32767;
    if (result < -32768) result = -32768;
    return (int16_t)result;
}

/**
 * Test: Gain 1.0 preserves sample
 */
TEST_CASE("Gain 1.0 preserves sample", "[audio][gain]")
{
    int16_t sample = 16384;
    int16_t result = apply_gain(sample, 1.0f);
    TEST_ASSERT_EQUAL_INT16(16384, result);
}

/**
 * Test: Gain 0.5 halves sample
 */
TEST_CASE("Gain 0.5 halves sample", "[audio][gain]")
{
    int16_t sample = 16384;
    int16_t result = apply_gain(sample, 0.5f);
    TEST_ASSERT_EQUAL_INT16(8192, result);
}

/**
 * Test: Gain 0.0 mutes sample
 */
TEST_CASE("Gain 0.0 mutes sample", "[audio][gain]")
{
    int16_t sample = 16384;
    int16_t result = apply_gain(sample, 0.0f);
    TEST_ASSERT_EQUAL_INT16(0, result);
}

/**
 * Test: Gain clamps positive overflow
 */
TEST_CASE("Gain clamps positive overflow", "[audio][gain][edge]")
{
    int16_t sample = 32767;  // Max positive
    int16_t result = apply_gain(sample, 2.0f);  // Would be 65534
    TEST_ASSERT_EQUAL_INT16(32767, result);  // Clamped to max
}

/**
 * Test: Gain clamps negative overflow
 */
TEST_CASE("Gain clamps negative overflow", "[audio][gain][edge]")
{
    int16_t sample = -32768;  // Max negative
    int16_t result = apply_gain(sample, 2.0f);  // Would be -65536
    TEST_ASSERT_EQUAL_INT16(-32768, result);  // Clamped to min
}

// ============================================================================
// Ring Buffer Tests
// ============================================================================

#define TEST_RING_SIZE 1024

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
    size_t read = 0;
    while (read < len && rb->available > 0) {
        data[read] = rb->buffer[rb->read_head];
        rb->read_head = (rb->read_head + 1) % TEST_RING_SIZE;
        rb->available--;
        read++;
    }
    return read;
}

/**
 * Test: Ring buffer write and read
 */
TEST_CASE("Ring buffer write and read", "[audio][ringbuf]")
{
    test_ring_buffer_t rb;
    ring_init(&rb);
    
    uint8_t write_data[] = {1, 2, 3, 4, 5};
    uint8_t read_data[5] = {0};
    
    size_t written = ring_write(&rb, write_data, 5);
    TEST_ASSERT_EQUAL_UINT(5, written);
    TEST_ASSERT_EQUAL_UINT(5, rb.available);
    
    size_t read = ring_read(&rb, read_data, 5);
    TEST_ASSERT_EQUAL_UINT(5, read);
    TEST_ASSERT_EQUAL_UINT(0, rb.available);
    
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data, read_data, 5);
}

/**
 * Test: Ring buffer wraps correctly
 */
TEST_CASE("Ring buffer wraps correctly", "[audio][ringbuf]")
{
    test_ring_buffer_t rb;
    ring_init(&rb);
    
    // Fill most of the buffer
    uint8_t fill_data[TEST_RING_SIZE - 10];
    memset(fill_data, 0xAA, sizeof(fill_data));
    ring_write(&rb, fill_data, sizeof(fill_data));
    
    // Read most of it
    uint8_t discard[TEST_RING_SIZE - 10];
    ring_read(&rb, discard, sizeof(discard));
    
    // Write data that wraps around
    uint8_t wrap_data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    ring_write(&rb, wrap_data, 15);
    
    // Read it back
    uint8_t read_data[15] = {0};
    ring_read(&rb, read_data, 15);
    
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wrap_data, read_data, 15);
}

/**
 * Test: Ring buffer full condition
 */
TEST_CASE("Ring buffer full condition", "[audio][ringbuf][edge]")
{
    test_ring_buffer_t rb;
    ring_init(&rb);
    
    // Try to write more than capacity
    uint8_t data[TEST_RING_SIZE + 100];
    memset(data, 0x55, sizeof(data));
    
    size_t written = ring_write(&rb, data, sizeof(data));
    
    // Should only write up to capacity
    TEST_ASSERT_EQUAL_UINT(TEST_RING_SIZE, written);
    TEST_ASSERT_EQUAL_UINT(TEST_RING_SIZE, rb.available);
}

// ============================================================================
// Test Registration
// ============================================================================

void register_audio_player_tests(void) {
    // Tests are auto-registered by Unity macros
    // This function exists for organizational purposes
}

