/**
 * @file test_runner.c
 * @brief Main test runner for CDJ DJ Deck test suite
 * 
 * Runs all registered Unity tests for the ESP32 CDJ project.
 * Execute with: idf.py -T test flash monitor
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "test_runner";

// External test registration functions
extern void register_audio_player_tests(void);
extern void register_waveform_view_tests(void);
extern void register_duration_calculation_tests(void);

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "CDJ DJ Deck Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS (needed by some components)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Run Unity tests
    UNITY_BEGIN();
    
    // Register all test groups
    register_duration_calculation_tests();
    register_waveform_view_tests();
    // register_audio_player_tests();  // Requires hardware, disabled by default
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
}

