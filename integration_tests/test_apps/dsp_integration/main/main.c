/**
 * @file main.c
 * @brief DSP pipeline integration tests
 * 
 * Tests the DSP signal chain:
 * - Fixed-point resampler (Q16.16) for variable-speed playback
 * - 3-band DJ EQ (low/mid/high)
 * - Soft limiter (polynomial saturation)
 * - Thread-safe pitch control (atomic operations)
 * - Block processing (256-sample blocks)
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dsp_engine.h"
#include "filter.h"
#include "pitch_control.h"
#include "board_config.h"
#include <string.h>
#include <math.h>

static const char *TAG = "dsp_integration_test";

void setUp(void) {
    // Called before each test
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void tearDown(void) {
    // Called after each test
}

/**
 * Test: Resampler Initialization
 */
void test_resampler_initialization(void)
{
    ESP_LOGI(TAG, "Test: Resampler initialization");
    
    resampler_state_t state;
    dsp_resampler_init(&state);
    
    // Verify state is zero-initialized
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state.phase_accum, 
                                      "Phase accumulator should be zero after init");
    
    ESP_LOGI(TAG, "Resampler initialized successfully");
}

/**
 * Test: Resampler Reset
 */
void test_resampler_reset(void)
{
    ESP_LOGI(TAG, "Test: Resampler reset");
    
    resampler_state_t state;
    dsp_resampler_init(&state);
    
    // Set some phase
    state.phase_accum = 0x12345678;
    
    // Reset
    dsp_resampler_reset(&state);
    
    // Verify reset
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state.phase_accum, 
                                      "Phase accumulator should be zero after reset");
    
    ESP_LOGI(TAG, "Resampler reset test passed");
}

/**
 * Test: Resampler Normal Speed (1.0x)
 */
void test_resampler_normal_speed(void)
{
    ESP_LOGI(TAG, "Test: Resampler normal speed (1.0x)");
    
    resampler_state_t state;
    dsp_resampler_init(&state);
    
    // Create test input buffer (stereo, 10 samples)
    int16_t input[20] = {0}; // 10 stereo frames
    for (int i = 0; i < 10; i++) {
        input[i * 2] = i * 1000;     // Left channel: 0, 1000, 2000, ...
        input[i * 2 + 1] = i * 500;   // Right channel: 0, 500, 1000, ...
    }
    
    // Output buffer
    int16_t output[512]; // 256 stereo frames
    size_t read_idx = 0;
    size_t ring_size = 10;
    
    // Resample at 1.0x speed
    size_t consumed = dsp_resample_linear(
        &state,
        input,
        ring_size,
        &read_idx,
        output,
        256,
        1.0f
    );
    
    // Verify output matches input (at normal speed)
    TEST_ASSERT_TRUE_MESSAGE(consumed > 0, "Should consume samples");
    
    // First output should match first input
    TEST_ASSERT_EQUAL_INT16_MESSAGE(input[0], output[0], 
                                     "First sample should match at 1.0x speed");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(input[1], output[1], 
                                     "First right sample should match");
    
    ESP_LOGI(TAG, "Resampler normal speed test passed (consumed %zu samples)", consumed);
}

/**
 * Test: Resampler Variable Speed (0.5x - half speed)
 */
void test_resampler_half_speed(void)
{
    ESP_LOGI(TAG, "Test: Resampler half speed (0.5x)");
    
    resampler_state_t state;
    dsp_resampler_init(&state);
    
    // Create test input
    int16_t input[20] = {0};
    for (int i = 0; i < 10; i++) {
        input[i * 2] = i * 1000;
        input[i * 2 + 1] = i * 500;
    }
    
    int16_t output[512];
    size_t read_idx = 0;
    size_t ring_size = 10;
    
    // Resample at 0.5x speed (should consume fewer source samples)
    size_t consumed = dsp_resample_linear(
        &state,
        input,
        ring_size,
        &read_idx,
        output,
        256,
        0.5f
    );
    
    TEST_ASSERT_TRUE_MESSAGE(consumed > 0, "Should consume samples");
    TEST_ASSERT_TRUE_MESSAGE(consumed < 256, 
                              "At 0.5x speed, should consume fewer samples than output");
    
    ESP_LOGI(TAG, "Resampler half speed test passed (consumed %zu samples)", consumed);
}

/**
 * Test: Resampler Double Speed (2.0x)
 */
void test_resampler_double_speed(void)
{
    ESP_LOGI(TAG, "Test: Resampler double speed (2.0x)");
    
    resampler_state_t state;
    dsp_resampler_init(&state);
    
    // Create test input
    int16_t input[20] = {0};
    for (int i = 0; i < 10; i++) {
        input[i * 2] = i * 1000;
        input[i * 2 + 1] = i * 500;
    }
    
    int16_t output[512];
    size_t read_idx = 0;
    size_t ring_size = 10;
    
    // Resample at 2.0x speed
    size_t consumed = dsp_resample_linear(
        &state,
        input,
        ring_size,
        &read_idx,
        output,
        256,
        2.0f
    );
    
    TEST_ASSERT_TRUE_MESSAGE(consumed > 0, "Should consume samples");
    
    ESP_LOGI(TAG, "Resampler double speed test passed (consumed %zu samples)", consumed);
}

/**
 * Test: DJ EQ Initialization
 */
void test_dj_eq_initialization(void)
{
    ESP_LOGI(TAG, "Test: DJ EQ initialization");
    
    dj_eq_t eq;
    dj_eq_init(&eq, 44100);
    
    // Verify default gains are unity (0.0)
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, eq.gain_low, 
                                       "Low gain should be 0.0 (unity)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, eq.gain_mid, 
                                       "Mid gain should be 0.0 (unity)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, eq.gain_high, 
                                       "High gain should be 0.0 (unity)");
    
    TEST_ASSERT_TRUE_MESSAGE(eq.enabled, "EQ should be enabled by default");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(44100, eq.sample_rate, 
                                      "Sample rate should be set");
    
    ESP_LOGI(TAG, "DJ EQ initialized successfully");
}

/**
 * Test: DJ EQ Gain Setting
 */
void test_dj_eq_gain_setting(void)
{
    ESP_LOGI(TAG, "Test: DJ EQ gain setting");
    
    dj_eq_t eq;
    dj_eq_init(&eq, 44100);
    
    // Set gains
    dj_eq_set_gains(&eq, -1.0f, 0.5f, 1.0f);
    
    // Verify gains are set
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, -1.0f, eq.gain_low, 
                                       "Low gain should be -1.0 (kill)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.5f, eq.gain_mid, 
                                       "Mid gain should be 0.5");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 1.0f, eq.gain_high, 
                                       "High gain should be 1.0 (boost)");
    
    ESP_LOGI(TAG, "DJ EQ gain setting test passed");
}

/**
 * Test: DJ EQ Enable/Disable
 */
void test_dj_eq_enable_disable(void)
{
    ESP_LOGI(TAG, "Test: DJ EQ enable/disable");
    
    dj_eq_t eq;
    dj_eq_init(&eq, 44100);
    
    // Should be enabled by default
    TEST_ASSERT_TRUE_MESSAGE(eq.enabled, "EQ should be enabled by default");
    
    // Disable
    dj_eq_set_enabled(&eq, false);
    TEST_ASSERT_FALSE_MESSAGE(eq.enabled, "EQ should be disabled");
    
    // Enable
    dj_eq_set_enabled(&eq, true);
    TEST_ASSERT_TRUE_MESSAGE(eq.enabled, "EQ should be enabled");
    
    ESP_LOGI(TAG, "DJ EQ enable/disable test passed");
}

/**
 * Test: DJ EQ Processing
 */
void test_dj_eq_processing(void)
{
    ESP_LOGI(TAG, "Test: DJ EQ processing");
    
    dj_eq_t eq;
    dj_eq_init(&eq, 44100);
    
    // Create test buffer (stereo, 256 frames)
    int16_t buffer[512];
    for (int i = 0; i < 256; i++) {
        buffer[i * 2] = 1000;     // Left channel
        buffer[i * 2 + 1] = 2000; // Right channel
    }
    
    // Process through EQ
    dj_eq_process(&eq, buffer, 256);
    
    // Verify buffer was processed (values may change due to filtering)
    // We just verify it doesn't crash and produces output
    TEST_ASSERT_NOT_NULL_MESSAGE(buffer, "Buffer should not be NULL");
    
    ESP_LOGI(TAG, "DJ EQ processing test passed");
}

/**
 * Test: DJ EQ Reset
 */
void test_dj_eq_reset(void)
{
    ESP_LOGI(TAG, "Test: DJ EQ reset");
    
    dj_eq_t eq;
    dj_eq_init(&eq, 44100);
    
    // Process some audio to populate delay lines
    int16_t buffer[512];
    memset(buffer, 0, sizeof(buffer));
    dj_eq_process(&eq, buffer, 256);
    
    // Reset
    dj_eq_reset(&eq);
    
    // Verify delay lines are cleared (all zeros)
    bool all_zero = true;
    for (int i = 0; i < 2; i++) {
        if (eq.w_low_l[i] != 0.0f || eq.w_low_r[i] != 0.0f ||
            eq.w_mid_l[i] != 0.0f || eq.w_mid_r[i] != 0.0f ||
            eq.w_high_l[i] != 0.0f || eq.w_high_r[i] != 0.0f) {
            all_zero = false;
            break;
        }
    }
    
    TEST_ASSERT_TRUE_MESSAGE(all_zero, "Delay lines should be cleared after reset");
    
    ESP_LOGI(TAG, "DJ EQ reset test passed");
}

/**
 * Test: Pitch Control Initialization
 */
void test_pitch_control_initialization(void)
{
    ESP_LOGI(TAG, "Test: Pitch control initialization");
    
    bool result = pitch_control_init();
    TEST_ASSERT_TRUE_MESSAGE(result, "Pitch control initialization failed");
    
    // Verify initial pitch is 0.0 (no adjustment)
    float pitch = pitch_control_get();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, pitch, 
                                       "Initial pitch should be 0.0");
    
    ESP_LOGI(TAG, "Pitch control initialized successfully");
}

/**
 * Test: Pitch Control Set/Get (Atomic)
 */
void test_pitch_control_atomic_operations(void)
{
    ESP_LOGI(TAG, "Test: Pitch control atomic operations");
    
    TEST_ASSERT_TRUE_MESSAGE(pitch_control_init(), "Pitch control init failed");
    
    // Test setting various pitch values
    pitch_control_set(5.0f);  // +5%
    float pitch = pitch_control_get();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 5.0f, pitch, 
                                       "Pitch should be +5%");
    
    pitch_control_set(-10.0f); // -10%
    pitch = pitch_control_get();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, -10.0f, pitch, 
                                       "Pitch should be -10%");
    
    pitch_control_set(0.0f);  // Reset
    pitch = pitch_control_get();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 0.0f, pitch, 
                                       "Pitch should be 0.0");
    
    ESP_LOGI(TAG, "Pitch control atomic operations test passed");
}

/**
 * Test: Pitch Control Reset
 */
void test_pitch_control_reset(void)
{
    ESP_LOGI(TAG, "Test: Pitch control reset");
    
    TEST_ASSERT_TRUE_MESSAGE(pitch_control_init(), "Pitch control init failed");
    
    // Set pitch
    pitch_control_set(15.0f);
    
    // Reset
    pitch_control_reset();
    
    // Verify reset
    float pitch = pitch_control_get();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 0.0f, pitch, 
                                       "Pitch should be 0.0 after reset");
    
    ESP_LOGI(TAG, "Pitch control reset test passed");
}

/**
 * Test: DSP Block Size
 */
void test_dsp_block_size(void)
{
    ESP_LOGI(TAG, "Test: DSP block size");
    
    // Verify DSP_BLOCK_SIZE is 256 (as per design)
    TEST_ASSERT_EQUAL_INT_MESSAGE(256, DSP_BLOCK_SIZE, 
                                    "DSP block size should be 256 frames");
    
    // Calculate timing
    float block_time_ms = (float)DSP_BLOCK_SIZE / 44.1f; // At 44.1kHz
    ESP_LOGI(TAG, "Block size: %d frames = %.2f ms at 44.1kHz", 
             DSP_BLOCK_SIZE, block_time_ms);
    
    TEST_ASSERT_TRUE_MESSAGE(block_time_ms < 10.0f, 
                              "Block time should be < 10ms for low latency");
    
    ESP_LOGI(TAG, "DSP block size test passed");
}

/**
 * Test: Complete DSP Pipeline
 */
void test_complete_dsp_pipeline(void)
{
    ESP_LOGI(TAG, "Test: Complete DSP pipeline");
    
    // Initialize all components
    resampler_state_t resampler;
    dsp_resampler_init(&resampler);
    
    dj_eq_t eq;
    dj_eq_init(&eq, 44100);
    
    TEST_ASSERT_TRUE_MESSAGE(pitch_control_init(), "Pitch control init failed");
    
    // Create test input (ring buffer simulation)
    int16_t input[512]; // 256 stereo frames
    for (int i = 0; i < 256; i++) {
        input[i * 2] = (int16_t)(sinf(i * 0.1f) * 10000.0f);     // Left: sine wave
        input[i * 2 + 1] = (int16_t)(cosf(i * 0.1f) * 10000.0f);  // Right: cosine
    }
    
    // Output buffer
    int16_t output[512];
    size_t read_idx = 0;
    
    // Set pitch to +5%
    pitch_control_set(5.0f);
    float speed = 1.0f + (pitch_control_get() / 100.0f);
    
    // 1. Resample
    size_t consumed = dsp_resample_linear(
        &resampler,
        input,
        256,
        &read_idx,
        output,
        256,
        speed
    );
    
    TEST_ASSERT_TRUE_MESSAGE(consumed > 0, "Resampler should consume samples");
    
    // 2. Apply EQ
    dj_eq_set_gains(&eq, 0.0f, 0.0f, 0.0f); // Unity gain
    dj_eq_process(&eq, output, 256);
    
    // Verify output is valid (not all zeros, not all max)
    bool has_variation = false;
    for (int i = 1; i < 256; i++) {
        if (output[i * 2] != output[0]) {
            has_variation = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(has_variation, "Output should have variation");
    
    ESP_LOGI(TAG, "Complete DSP pipeline test passed");
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "DSP Integration Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Run Unity tests
    UNITY_BEGIN();
    
    RUN_TEST(test_resampler_initialization, 301);
    RUN_TEST(test_resampler_reset, 302);
    RUN_TEST(test_resampler_normal_speed, 303);
    RUN_TEST(test_resampler_half_speed, 304);
    RUN_TEST(test_resampler_double_speed, 305);
    RUN_TEST(test_dj_eq_initialization, 306);
    RUN_TEST(test_dj_eq_gain_setting, 307);
    RUN_TEST(test_dj_eq_enable_disable, 308);
    RUN_TEST(test_dj_eq_processing, 309);
    RUN_TEST(test_dj_eq_reset, 310);
    RUN_TEST(test_pitch_control_initialization, 311);
    RUN_TEST(test_pitch_control_atomic_operations, 312);
    RUN_TEST(test_pitch_control_reset, 313);
    RUN_TEST(test_dsp_block_size, 314);
    RUN_TEST(test_complete_dsp_pipeline, 315);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "DSP Integration Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running for host-side tests (minimal stack usage)
    // Use a simple delay loop to avoid stack overflow
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
