/**
 * @file test_runner.c
 * @brief Main test runner for CDJ DJ Deck test suite
 * 
 * Runs all registered Unity tests for the ESP32 CDJ project.
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "test_runner";

// External test registration functions
extern void register_duration_calculation_tests(void);
extern void register_waveform_view_tests(void);
extern void register_audio_player_tests(void);

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

void app_main(void) {
    // Wait for serial to stabilize
    vTaskDelay(pdMS_TO_TICKS(500));
    
    printf("\n\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    CDJ DJ Deck Test Suite v1.0");
    ESP_LOGI(TAG, "========================================");
    printf("\n");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Start Unity test framework
    UNITY_BEGIN();
    
    // Run all test groups
    register_duration_calculation_tests();
    register_waveform_view_tests();
    register_audio_player_tests();
    
    // Finish and report
    int failures = UNITY_END();
    
    printf("\n");
    ESP_LOGI(TAG, "========================================");
    if (failures == 0) {
        ESP_LOGI(TAG, "    ALL TESTS PASSED!");
    } else {
        ESP_LOGE(TAG, "    %d TEST(S) FAILED!", failures);
    }
    ESP_LOGI(TAG, "========================================");
    printf("\n");
    
    // Keep device running so output can be read
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
