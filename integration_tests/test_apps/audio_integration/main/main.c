/**
 * @file main.c
 * @brief Audio hardware integration tests
 * 
 * Tests I2S initialization, audio output, and MP3 playback integration
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_output.h"
#include "audio_player.h"
#include "storage.h"
#include "board_config.h"
#include <string.h>

static const char *TAG = "audio_integration_test";

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
 * Test: I2S Initialization
 */
void test_i2s_initialization(void)
{
    ESP_LOGI(TAG, "Test: I2S initialization");
    
    bool result = audio_output_init();
    TEST_ASSERT_TRUE_MESSAGE(result, "I2S audio output initialization failed");
    
    // Verify sample rate
    uint32_t sample_rate = audio_output_get_rate();
    TEST_ASSERT_EQUAL_INT_MESSAGE(44100, sample_rate, "Sample rate should be 44100 Hz");
    
    ESP_LOGI(TAG, "I2S initialized successfully: %lu Hz", sample_rate);
}

/**
 * Test: Audio Output Configuration
 */
void test_audio_output_configuration(void)
{
    ESP_LOGI(TAG, "Test: Audio output configuration");
    
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    
    // Test sample rate setting
    bool result = audio_output_set_rate(48000);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to set sample rate to 48000");
    
    uint32_t rate = audio_output_get_rate();
    TEST_ASSERT_EQUAL_INT_MESSAGE(48000, rate, "Sample rate not set correctly");
    
    // Reset to 44100
    audio_output_set_rate(44100);
    
    // Test channel setting
    result = audio_output_set_channels(2);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to set channels to stereo");
    
    ESP_LOGI(TAG, "Audio output configuration test passed");
}

/**
 * Test: Storage Initialization for Audio Files
 */
void test_storage_initialization(void)
{
    ESP_LOGI(TAG, "Test: Storage initialization");
    
    bool result = storage_init();
    if (!result) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    ESP_LOGI(TAG, "Storage initialized successfully");
    TEST_ASSERT_TRUE_MESSAGE(result, "Storage initialization failed");
}

/**
 * Test: Audio Player Initialization
 */
void test_audio_player_initialization(void)
{
    ESP_LOGI(TAG, "Test: Audio player initialization");
    
    // Initialize audio output first
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    
    // Initialize storage if available
    storage_init(); // Non-critical
    
    // Initialize audio player
    bool result = audio_player_init();
    TEST_ASSERT_TRUE_MESSAGE(result, "Audio player initialization failed");
    
    ESP_LOGI(TAG, "Audio player initialized successfully");
}

/**
 * Test: MP3 File Loading (requires storage with test file)
 */
void test_mp3_file_loading(void)
{
    ESP_LOGI(TAG, "Test: MP3 file loading");
    
    // Initialize systems
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    TEST_ASSERT_TRUE_MESSAGE(audio_player_init(), "Audio player init failed");
    
    if (!storage_init()) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    // Try to load a test file (if available)
    const char* test_file = "/sdcard/test_tone_1khz.mp3";
    bool result = audio_player_load(test_file);
    
    if (!result) {
        ESP_LOGW(TAG, "Test file %s not found - this is expected if file doesn't exist", test_file);
        TEST_IGNORE_MESSAGE("Test MP3 file not available");
        return;
    }
    
    ESP_LOGI(TAG, "MP3 file loaded successfully: %s", test_file);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to load MP3 file");
}

/**
 * Test: Playback State Transitions
 */
void test_playback_state_transitions(void)
{
    ESP_LOGI(TAG, "Test: Playback state transitions");
    
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    TEST_ASSERT_TRUE_MESSAGE(audio_player_init(), "Audio player init failed");
    
    // Check initial state
    audio_player_state_t state = audio_player_get_state();
    TEST_ASSERT_EQUAL_INT_MESSAGE(AUDIO_PLAYER_STATE_STOPPED, state, 
                                   "Initial state should be STOPPED");
    
    ESP_LOGI(TAG, "Playback state transitions test passed");
}

/**
 * Test: Waveform Data Generation
 */
void test_waveform_generation(void)
{
    ESP_LOGI(TAG, "Test: Waveform data generation");
    
    // Initialize
    audio_player_init();
    
    // Retrieve waveform buffer
    uint8_t wave_buf[100];
    memset(wave_buf, 0xAA, sizeof(wave_buf));
    
    audio_player_get_waveform(wave_buf, sizeof(wave_buf));
    
    // Verify buffer was written to (should not be 0xAA everywhere)
    bool changed = false;
    for (int i = 0; i < sizeof(wave_buf); i++) {
        if (wave_buf[i] != 0xAA) {
            changed = true;
            break;
        }
    }
    
    TEST_ASSERT_TRUE_MESSAGE(changed, "Waveform buffer should be updated by audio player");
    
    ESP_LOGI(TAG, "Waveform generation test passed");
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Audio Integration Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Run Unity tests
    UNITY_BEGIN();
    
    RUN_TEST(test_i2s_initialization, 203);
    RUN_TEST(test_audio_output_configuration, 204);
    RUN_TEST(test_storage_initialization, 205);
    RUN_TEST(test_audio_player_initialization, 206);
    RUN_TEST(test_mp3_file_loading, 207);
    RUN_TEST(test_playback_state_transitions, 208);
    RUN_TEST(test_waveform_generation, 209);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Audio Integration Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running for host-side tests
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}