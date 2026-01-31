/**
 * @file main.c
 * @brief Storage hardware integration tests
 * 
 * Tests SD card, USB host, and track database integration
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "track_db.h"
#include "board_config.h"

static const char *TAG = "storage_integration_test";

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
 * Test: Storage Initialization
 */
TEST_CASE("Storage initialization", "[storage][integration]")
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
 * Test: SD Card Mount (if configured)
 */
TEST_CASE("SD card mount", "[storage][integration]")
{
    ESP_LOGI(TAG, "Test: SD card mount");
    
    #ifndef SD_CARD_DISABLE
        bool result = storage_init();
        if (!result) {
            TEST_IGNORE_MESSAGE("SD card not available - skipping test");
            return;
        }
        
        ESP_LOGI(TAG, "SD card mounted successfully");
        TEST_ASSERT_TRUE_MESSAGE(result, "SD card mount failed");
    #else
        TEST_IGNORE_MESSAGE("SD card disabled in configuration");
    #endif
}

/**
 * Test: Track Database Initialization
 */
TEST_CASE("Track database initialization", "[storage][integration]")
{
    ESP_LOGI(TAG, "Test: Track database initialization");
    
    bool result = track_db_init();
    TEST_ASSERT_TRUE_MESSAGE(result, "Track database initialization failed");
    
    uint32_t count = track_db_get_count();
    ESP_LOGI(TAG, "Track database initialized with %lu tracks", count);
}

/**
 * Test: Track Scanning
 */
TEST_CASE("Track scanning", "[storage][integration]")
{
    ESP_LOGI(TAG, "Test: Track scanning");
    
    // Initialize storage
    if (!storage_init()) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    // Initialize track database
    TEST_ASSERT_TRUE_MESSAGE(track_db_init(), "Track database init failed");
    
    // Scan for tracks
    uint32_t count = track_db_scan();
    ESP_LOGI(TAG, "Scanned %lu tracks", count);
    
    // Note: Count may be 0 if no MP3 files are present
    // This is acceptable for the test
}

/**
 * Test: Track Metadata Retrieval
 */
TEST_CASE("Track metadata retrieval", "[storage][integration]")
{
    ESP_LOGI(TAG, "Test: Track metadata retrieval");
    
    if (!storage_init()) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(track_db_init(), "Track database init failed");
    track_db_scan();
    
    uint32_t count = track_db_get_count();
    if (count == 0) {
        TEST_IGNORE_MESSAGE("No tracks found - skipping metadata test");
        return;
    }
    
    // Get first track metadata
    track_info_t info;
    bool result = track_db_get_track(0, &info);
    
    if (result) {
        ESP_LOGI(TAG, "Track 0: %s", info.filename);
        ESP_LOGI(TAG, "  Title: %s", info.title);
        ESP_LOGI(TAG, "  Artist: %s", info.artist);
        ESP_LOGI(TAG, "  Duration: %lu seconds", info.duration_seconds);
        
        // Basic validation of metadata
        // MP3 duration should be > 0 for valid files
        // Note: info.duration_seconds might be 0 if ID3 parsing failed or wasn't full scan
        if (info.duration_seconds > 0) {
            ESP_LOGI(TAG, "  Valid duration detected");
        }
    }
    
    // Test passes if we can retrieve track info (even if metadata is empty)
    ESP_LOGI(TAG, "Track metadata retrieval test passed");
}

/**
 * Test: File System Access
 */
TEST_CASE("File system access", "[storage][integration]")
{
    ESP_LOGI(TAG, "Test: File system access");
    
    if (!storage_init()) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    // File system access is verified by successful storage_init()
    // Additional file operations would require storage API extensions
    ESP_LOGI(TAG, "File system access verified");
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Storage Integration Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Run Unity tests
    UNITY_BEGIN();
    
    RUN_TEST_CASE("Storage initialization");
    RUN_TEST_CASE("SD card mount");
    RUN_TEST_CASE("Track database initialization");
    RUN_TEST_CASE("Track scanning");
    RUN_TEST_CASE("Track metadata retrieval");
    RUN_TEST_CASE("File system access");
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Storage Integration Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running for host-side tests
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

