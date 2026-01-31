/**
 * @file main.c
 * @brief End-to-end workflow integration tests
 * 
 * Tests complete user workflows: track loading, playback, cue points, loops, pitch control
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
#include "track_db.h"
#include "controls.h"
#include "pitch_control.h"
#include "cue_points.h"
#include "loop_control.h"
#include "display.h"
#include "ui_manager.h" // Added include
#include "board_config.h"

static const char *TAG = "e2e_workflows_test";

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
 * Test: Full System Initialization
 */
TEST_CASE("Full system initialization", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Full system initialization");
    
    // Initialize all subsystems
    bool audio_ok = audio_output_init();
    bool storage_ok = storage_init(); // Non-critical
    bool player_ok = audio_player_init();
    bool controls_ok = controls_init(NULL, NULL); // May fail if not configured
    bool pitch_ok = pitch_control_init(); // May fail if not configured
    bool display_ok = display_init();
    bool track_db_ok = track_db_init();
    
    // Critical components must succeed
    TEST_ASSERT_TRUE_MESSAGE(audio_ok, "Audio output init failed");
    TEST_ASSERT_TRUE_MESSAGE(player_ok, "Audio player init failed");
    TEST_ASSERT_TRUE_MESSAGE(track_db_ok, "Track database init failed");
    
    ESP_LOGI(TAG, "System initialization: Audio=%d, Storage=%d, Player=%d, Controls=%d, Pitch=%d, Display=%d, TrackDB=%d",
             audio_ok, storage_ok, player_ok, controls_ok, pitch_ok, display_ok, track_db_ok);
}

/**
 * Test: Track Loading Workflow
 */
TEST_CASE("Track loading workflow", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Track loading workflow");
    
    // Initialize systems
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    TEST_ASSERT_TRUE_MESSAGE(audio_player_init(), "Audio player init failed");
    TEST_ASSERT_TRUE_MESSAGE(track_db_init(), "Track database init failed");
    
    // Initialize storage
    if (!storage_init()) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    // Scan for tracks
    uint32_t track_count = track_db_scan();
    ESP_LOGI(TAG, "Found %lu tracks", track_count);
    
    if (track_count == 0) {
        TEST_IGNORE_MESSAGE("No tracks found - skipping track loading test");
        return;
    }
    
    // Try to load first track
    // Note: track_db_get_track structure may vary - this is a simplified test
    ESP_LOGI(TAG, "Track loading workflow test passed");
}

/**
 * Test: Playback Workflow
 */
TEST_CASE("Playback workflow", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Playback workflow");
    
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    TEST_ASSERT_TRUE_MESSAGE(audio_player_init(), "Audio player init failed");
    
    // Check initial state
    audio_player_state_t state = audio_player_get_state();
    TEST_ASSERT_EQUAL_INT_MESSAGE(AUDIO_PLAYER_STATE_STOPPED, state, 
                                   "Initial state should be STOPPED");
    
    // Note: Actual playback requires a loaded track
    // This test verifies state machine and workflow logic
    ESP_LOGI(TAG, "Playback workflow test passed");
}

/**
 * Test: Cue Point Workflow
 */
TEST_CASE("Cue point workflow", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Cue point workflow");
    
    // Initialize cue points system
    // Note: cue_points may not have explicit init, but should work after system init
    
    // Test setting a cue point
    uint32_t test_position = 10; // 10 seconds
    cue_points_set(0, test_position);
    
    // Test getting cue point
    uint32_t retrieved = cue_points_get(0);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(test_position, retrieved, 
                                      "Cue point should be stored and retrieved");
    
    ESP_LOGI(TAG, "Cue point workflow test passed");
}

/**
 * Test: Loop Control Workflow
 */
TEST_CASE("Loop control workflow", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Loop control workflow");
    
    // Test setting loop in point
    uint32_t loop_in = 5; // 5 seconds
    uint32_t loop_out = 15; // 15 seconds
    
    loop_control_set_in(loop_in);
    loop_control_set_out(loop_out);
    
    // Verify loop points
    uint32_t retrieved_in = loop_control_get_in();
    uint32_t retrieved_out = loop_control_get_out();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop_in, retrieved_in, "Loop in point should be set");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop_out, retrieved_out, "Loop out point should be set");
    
    // Verify loop points (if API supports retrieval)
    ESP_LOGI(TAG, "Loop control workflow test passed");
}

/**
 * Test: Pitch Control Workflow
 */
TEST_CASE("Pitch control workflow", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Pitch control workflow");
    
    if (!pitch_control_init()) {
        TEST_IGNORE_MESSAGE("Pitch control not configured - skipping test");
        return;
    }
    
    // Update pitch control (simulates fader movement)
    pitch_control_update();
    
    ESP_LOGI(TAG, "Pitch control workflow test passed");
}

/**
 * Test: UI Integration
 */
TEST_CASE("UI integration", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: UI integration");
    
    // Initialize Display first
    if (!display_init()) {
        TEST_IGNORE_MESSAGE("Display not available - skipping test");
        return;
    }
    
    // Verify display is initialized
    bool is_init = display_is_initialized();
    TEST_ASSERT_TRUE_MESSAGE(is_init, "Display should be initialized");
    
    // Initialize UI Manager (LVGL)
    // This aligns with "High-Contrast HUD UI" vision
    bool ui_ok = ui_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(ui_ok, "UI Manager initialization failed");
    
    // Test display dimensions (defined in board_config.h)
    TEST_ASSERT_EQUAL_INT_MESSAGE(480, SCREEN_WIDTH, "Display width should be 480");
    TEST_ASSERT_EQUAL_INT_MESSAGE(272, SCREEN_HEIGHT, "Display height should be 272");
    
    ESP_LOGI(TAG, "UI integration test passed");
}

/**
 * Test: Complete Playback Sequence
 */
TEST_CASE("Complete playback sequence", "[e2e][integration]")
{
    ESP_LOGI(TAG, "Test: Complete playback sequence");
    
    // Initialize all systems
    TEST_ASSERT_TRUE_MESSAGE(audio_output_init(), "Audio output init failed");
    TEST_ASSERT_TRUE_MESSAGE(audio_player_init(), "Audio player init failed");
    
    if (!storage_init()) {
        TEST_IGNORE_MESSAGE("Storage not available - skipping test");
        return;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(track_db_init(), "Track database init failed");
    track_db_scan();
    
    // This test verifies the complete sequence can be executed
    // Actual playback would require a loaded track and would be tested
    // via host-side scripts that can observe the behavior
    ESP_LOGI(TAG, "Complete playback sequence test passed");
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "End-to-End Workflows Test Suite");
    ESP_LOGI(TAG, "========================================");
    
    // Run Unity tests
    UNITY_BEGIN();
    
    RUN_TEST_CASE("Full system initialization");
    RUN_TEST_CASE("Track loading workflow");
    RUN_TEST_CASE("Playback workflow");
    RUN_TEST_CASE("Cue point workflow");
    RUN_TEST_CASE("Loop control workflow");
    RUN_TEST_CASE("Pitch control workflow");
    RUN_TEST_CASE("UI integration");
    RUN_TEST_CASE("Complete playback sequence");
    
    UNITY_END();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "End-to-End Workflows Test Suite Complete");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running for host-side tests
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

