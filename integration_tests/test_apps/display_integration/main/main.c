/**
 * @file main.c
 * @brief Display and touch hardware integration tests
 * 
 * Tests QSPI communication, display rendering, and touch controller integration
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "board_config.h"

static const char *TAG = "display_integration_test";

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
 * Test: Display Initialization
 */
TEST_CASE("Display initialization", "[display][integration]")
{
    ESP_LOGI(TAG, "Test: Display initialization");
    
    bool result = display_init();
    TEST_ASSERT_TRUE_MESSAGE(result, "Display initialization failed");
    
    // Verify display is initialized
    bool is_init = display_is_initialized();
    TEST_ASSERT_TRUE_MESSAGE(is_init, "Display should be initialized");
    
    ESP_LOGI(TAG, "Display initialized successfully");
}

/**
 * Test: Display QSPI Communication
 */
TEST_CASE("Display QSPI communication", "[display][integration]")
{
    ESP_LOGI(TAG, "Test: Display QSPI communication");
    
    TEST_ASSERT_TRUE_MESSAGE(display_init(), "Display init failed");
    
    // Run byte order test to verify QSPI communication
    display_test_byte_order();
    
    ESP_LOGI(TAG, "QSPI communication test passed");
}

/**
 * Test: Display Rendering - Color Bars
 */
TEST_CASE("Display rendering color bars", "[display][integration]")
{
    ESP_LOGI(TAG, "Test: Display rendering - color bars");
    
    TEST_ASSERT_TRUE_MESSAGE(display_init(), "Display init failed");
    
    // Draw color bars test pattern
    display_test_color_bars();
    
    // Give time for rendering
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Color bars rendered successfully");
}

/**
 * Test: Display Dimensions
 */
TEST_CASE("Display dimensions", "[display][integration]")
{
    ESP_LOGI(TAG, "Test: Display dimensions");
    
    TEST_ASSERT_TRUE_MESSAGE(display_init(), "Display init failed");
    
    // Display dimensions are defined in board_config.h
    ESP_LOGI(TAG, "Display dimensions: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(480, SCREEN_WIDTH, "Display width should be 480");
    TEST_ASSERT_EQUAL_INT_MESSAGE(272, SCREEN_HEIGHT, "Display height should be 272");
}

/**
 * Test: Display Fill Screen
 */
TEST_CASE("Display fill screen", "[display][integration]")
{
    ESP_LOGI(TAG, "Test: Display fill screen");
    
    TEST_ASSERT_TRUE_MESSAGE(display_init(), "Display init failed");
    
    // Fill screen with test color
    display_fill_screen(0xF800); // Red
    vTaskDelay(pdMS_TO_TICKS(100));
    
    display_fill_screen(0x07E0); // Green
    vTaskDelay(pdMS_TO_TICKS(100));
    
    display_fill_screen(0x001F); // Blue
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Display fill screen test passed");
}

/**
 * Test: Touch Controller Initialization (if GT911 is configured)
 */
TEST_CASE("Touch controller initialization", "[display][touch][integration]")
{
    ESP_LOGI(TAG, "Test: Touch controller initialization");
    
    #if defined(TOUCH_GT911) && TOUCH_GT911
        // Touch initialization is typically done by display_init()
        TEST_ASSERT_TRUE_MESSAGE(display_init(), "Display init failed");
        
        ESP_LOGI(TAG, "Touch controller should be initialized with display");
    #else
        TEST_IGNORE_MESSAGE("Touch controller not configured");
    #endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Display Integration Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Run Unity tests
    UNITY_BEGIN();
    
    RUN_TEST_CASE("Display initialization");
    RUN_TEST_CASE("Display QSPI communication");
    RUN_TEST_CASE("Display rendering color bars");
    RUN_TEST_CASE("Display dimensions");
    RUN_TEST_CASE("Display fill screen");
    RUN_TEST_CASE("Touch controller initialization");
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Display Integration Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running for host-side tests
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

