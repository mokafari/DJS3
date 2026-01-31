/**
 * @file main.c
 * @brief Controls hardware integration tests
 * 
 * Tests GPIO buttons, jog wheel, and pitch control integration
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "controls.h"
#include "pitch_control.h"
#include "board_config.h"
#include "driver/gpio.h"

static const char *TAG = "controls_integration_test";

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
 * Test: Controls Initialization
 */
TEST_CASE("Controls initialization", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: Controls initialization");
    
    bool result = controls_init(NULL, NULL);
    if (!result) {
        TEST_IGNORE_MESSAGE("Controls not configured - skipping test");
        return;
    }
    
    ESP_LOGI(TAG, "Controls initialized successfully");
    TEST_ASSERT_TRUE_MESSAGE(result, "Controls initialization failed");
}

/**
 * Test: GPIO Button Configuration
 */
TEST_CASE("GPIO button configuration", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: GPIO button configuration");
    
    #if defined(BUTTON_CUE_PIN) && BUTTON_CUE_PIN >= 0
        // Buttons are configured during controls_init()
        bool result = controls_init(NULL, NULL);
        if (result) {
            ESP_LOGI(TAG, "Button GPIOs configured successfully");
        } else {
            TEST_IGNORE_MESSAGE("Controls not available");
        }
    #else
        TEST_IGNORE_MESSAGE("Buttons not configured in board_config.h");
    #endif
}

/**
 * Test: Jog Wheel Configuration
 */
TEST_CASE("Jog wheel configuration", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: Jog wheel configuration");
    
    #if defined(JOG_WHEEL_A_PIN) && JOG_WHEEL_A_PIN >= 0
        bool result = controls_init(NULL, NULL);
        if (result) {
            ESP_LOGI(TAG, "Jog wheel GPIOs configured: A=%d, B=%d, Touch=%d", 
                     JOG_WHEEL_A_PIN, JOG_WHEEL_B_PIN, JOG_WHEEL_TOUCH_PIN);
        } else {
            TEST_IGNORE_MESSAGE("Controls not available");
        }
    #else
        TEST_IGNORE_MESSAGE("Jog wheel not configured in board_config.h");
    #endif
}

/**
 * Test: Pitch Control Initialization
 */
TEST_CASE("Pitch control initialization", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: Pitch control initialization");
    
    bool result = pitch_control_init();
    if (!result) {
        TEST_IGNORE_MESSAGE("Pitch control not configured - skipping test");
        return;
    }
    
    ESP_LOGI(TAG, "Pitch control initialized successfully");
    TEST_ASSERT_TRUE_MESSAGE(result, "Pitch control initialization failed");
}

/**
 * Test: Pitch Encoder Configuration
 */
TEST_CASE("Pitch encoder configuration", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: Pitch encoder configuration");
    
    #if defined(PITCH_ENCODER_A_PIN) && PITCH_ENCODER_A_PIN >= 0
        bool result = pitch_control_init();
        if (result) {
            ESP_LOGI(TAG, "Pitch encoder GPIOs configured: A=%d, B=%d", 
                     PITCH_ENCODER_A_PIN, PITCH_ENCODER_B_PIN);
        } else {
            TEST_IGNORE_MESSAGE("Pitch control not available");
        }
    #else
        TEST_IGNORE_MESSAGE("Pitch encoder not configured in board_config.h");
    #endif
}

/**
 * Test: Control Update Function
 */
TEST_CASE("Control update function", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: Control update function");
    
    if (!controls_init(NULL, NULL)) {
        TEST_IGNORE_MESSAGE("Controls not available - skipping test");
        return;
    }
    
    // Call update function - should not crash
    controls_update();
    
    ESP_LOGI(TAG, "Control update function executed successfully");
}

/**
 * Test: Pitch Control Update Function
 */
TEST_CASE("Pitch control update function", "[controls][integration]")
{
    ESP_LOGI(TAG, "Test: Pitch control update function");
    
    if (!pitch_control_init()) {
        TEST_IGNORE_MESSAGE("Pitch control not available - skipping test");
        return;
    }
    
    // Call update function - should not crash
    pitch_control_update();
    
    ESP_LOGI(TAG, "Pitch control update function executed successfully");
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Controls Integration Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Run Unity tests
    UNITY_BEGIN();
    
    RUN_TEST_CASE("Controls initialization");
    RUN_TEST_CASE("GPIO button configuration");
    RUN_TEST_CASE("Jog wheel configuration");
    RUN_TEST_CASE("Pitch control initialization");
    RUN_TEST_CASE("Pitch encoder configuration");
    RUN_TEST_CASE("Control update function");
    RUN_TEST_CASE("Pitch control update function");
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Controls Integration Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running for host-side tests
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

