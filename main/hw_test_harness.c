/**
 * @file hw_test_harness.c
 * @brief Hardware test harness implementation
 * 
 * Tests actual DJ deck components on real hardware.
 * Output is structured for LLM parsing.
 */

#include "hw_test_harness.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

// Component headers
#include "storage.h"
#include "track_db.h"
#include "audio_player.h"
#include "controls.h"
#include "display.h"
#include "ui_manager.h"
#include "waveform_view.h"

static const char *TAG = "hw_test";

// ============================================================================
// Test Output Macros (LLM-Friendly Format)
// ============================================================================

#define TEST_PASS(name, fmt, ...) do { \
    printf("TEST_PASS|%s|" fmt "\n", name, ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

#define TEST_FAIL(name, fmt, ...) do { \
    printf("TEST_FAIL|%s|" fmt "\n", name, ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

#define TEST_INFO(name, fmt, ...) do { \
    printf("TEST_INFO|%s|" fmt "\n", name, ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

// ============================================================================
// Storage Tests
// ============================================================================

static int test_storage_init(void) {
    ESP_LOGI(TAG, "Testing storage initialization...");
    
    if (!storage_init()) {
        TEST_FAIL("storage_init", "Failed to initialize storage");
        return -1;
    }
    
    if (!storage_auto_select()) {
        TEST_FAIL("storage_init", "No storage source available");
        return -1;
    }
    
    const char *mount = storage_get_mount_point();
    if (mount == NULL) {
        TEST_FAIL("storage_init", "Mount point is NULL");
        return -1;
    }
    
    storage_source_t source = storage_get_active_source();
    const char *source_name = (source == STORAGE_SOURCE_SD_CARD) ? "SD Card" : 
                               (source == STORAGE_SOURCE_USB) ? "USB" : "None";
    
    TEST_PASS("storage_init", "Initialized: %s at %s", source_name, mount);
    return 0;
}

static int test_storage_available(void) {
    if (!storage_is_available()) {
        TEST_FAIL("storage_available", "Storage not available");
        return -1;
    }
    
    TEST_PASS("storage_available", "Storage is accessible");
    return 0;
}

// ============================================================================
// Track Database Tests
// ============================================================================

static int test_track_db_init(void) {
    ESP_LOGI(TAG, "Testing track database initialization...");
    
    if (!track_db_init()) {
        TEST_FAIL("track_db_init", "Failed to initialize track database");
        return -1;
    }
    
    TEST_PASS("track_db_init", "Track database initialized");
    return 0;
}

static int test_track_db_scan(void) {
    ESP_LOGI(TAG, "Scanning for tracks...");
    
    uint32_t count = track_db_scan();
    
    if (count == 0) {
        TEST_FAIL("track_db_scan", "No tracks found");
        return -1;
    }
    
    TEST_PASS("track_db_scan", "Found %lu tracks", (unsigned long)count);
    
    // Log first few tracks for debugging
    for (uint32_t i = 0; i < count && i < 3; i++) {
        track_info_t info;
        if (track_db_get_track(i, &info)) {
            TEST_INFO("track_db_scan", "Track %lu: %s (ID3: %s)", 
                     (unsigned long)i, 
                     info.has_id3 ? info.title : info.filename,
                     info.has_id3 ? "yes" : "no");
        }
    }
    
    return 0;
}

// ============================================================================
// Audio Player Tests
// ============================================================================

static int test_audio_init(void) {
    ESP_LOGI(TAG, "Testing audio player initialization...");
    
    if (!audio_player_init()) {
        TEST_FAIL("audio_init", "Failed to initialize audio player");
        return -1;
    }
    
    TEST_PASS("audio_init", "Audio player initialized");
    return 0;
}

static int test_audio_load(void) {
    ESP_LOGI(TAG, "Testing audio load...");
    
    // Get first track from database
    if (track_db_get_count() == 0) {
        TEST_FAIL("audio_load", "No tracks available to load");
        return -1;
    }
    
    track_info_t info;
    if (!track_db_get_track(0, &info)) {
        TEST_FAIL("audio_load", "Failed to get track info");
        return -1;
    }
    
    if (!audio_player_load(info.filename)) {
        TEST_FAIL("audio_load", "Failed to load: %s", info.filename);
        return -1;
    }
    
    // Wait for decoder to start processing (populates duration and waveform)
    // The decoder runs in a background task
    ESP_LOGI(TAG, "Waiting for decoder to process first frames...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    const char *title = audio_player_get_track_title();
    TEST_PASS("audio_load", "Loaded: %s", title ? title : info.filename);
    return 0;
}

static int test_audio_duration(void) {
    // Duration is calculated after first frame decode, may need to wait
    uint32_t duration = 0;
    
    // Poll for duration (decoder task calculates it asynchronously)
    for (int i = 0; i < 20; i++) {  // Max 2 seconds wait
        duration = audio_player_get_duration();
        if (duration > 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (duration == 0) {
        TEST_FAIL("audio_duration", "Duration is 0 after 2s wait");
        return -1;
    }
    
    // Duration should be reasonable (between 10s and 20min)
    if (duration < 10 || duration > 1200) {
        TEST_FAIL("audio_duration", "Duration out of range: %lu seconds", (unsigned long)duration);
        return -1;
    }
    
    TEST_PASS("audio_duration", "Duration: %lu seconds", (unsigned long)duration);
    return 0;
}

static int test_audio_play_pause(void) {
    ESP_LOGI(TAG, "Testing play/pause...");
    
    // Start playback
    if (!audio_player_play()) {
        TEST_FAIL("audio_play", "Failed to start playback");
        return -1;
    }
    
    // Wait for play command to be processed (async via queue)
    // Poll for PLAYING state
    bool is_playing = false;
    for (int i = 0; i < 20; i++) {  // Max 2 seconds wait
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
            is_playing = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (!is_playing) {
        TEST_FAIL("audio_play", "State not PLAYING after 2s wait (state=%d)", 
                  audio_player_get_state());
        return -1;
    }
    
    // Check position is advancing
    uint32_t pos1 = audio_player_get_position();
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t pos2 = audio_player_get_position();
    
    // Pause
    audio_player_pause();
    
    // Wait for pause command to be processed
    bool is_paused = false;
    for (int i = 0; i < 10; i++) {
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PAUSED) {
            is_paused = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (!is_paused) {
        TEST_FAIL("audio_play", "State not PAUSED after pause()");
        return -1;
    }
    
    TEST_PASS("audio_play", "Play/pause working, position: %lu -> %lu", 
              (unsigned long)pos1, (unsigned long)pos2);
    return 0;
}

static int test_waveform_data(void) {
    ESP_LOGI(TAG, "Testing waveform data...");
    
    // Ensure playback is running to populate waveform buffer
    audio_player_resume();
    
    // Wait for waveform data to be populated
    uint8_t waveform[480];
    int non_zero = 0;
    int max_val = 0;
    
    // Poll for non-zero waveform data (decoder fills this as it decodes)
    for (int attempt = 0; attempt < 30; attempt++) {  // Max 3 seconds
        memset(waveform, 0, sizeof(waveform));
        audio_player_get_waveform(waveform, 480);
        
        non_zero = 0;
        max_val = 0;
        for (int i = 0; i < 480; i++) {
            if (waveform[i] > 0) non_zero++;
            if (waveform[i] > max_val) max_val = waveform[i];
        }
        
        if (non_zero > 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    audio_player_pause();
    
    if (non_zero == 0) {
        TEST_FAIL("waveform_data", "Waveform is all zeros after 3s wait");
        return -1;
    }
    
    TEST_PASS("waveform_data", "480 bytes, %d non-zero, max=%d", non_zero, max_val);
    return 0;
}

// ============================================================================
// Display/UI Tests
// ============================================================================

static int test_display_init(void) {
    ESP_LOGI(TAG, "Testing display initialization...");
    
    if (!display_init()) {
        TEST_FAIL("display_init", "Failed to initialize display");
        return -1;
    }
    
    if (!display_is_initialized()) {
        TEST_FAIL("display_init", "Display not marked as initialized");
        return -1;
    }
    
    TEST_PASS("display_init", "Display initialized (480x272)");
    return 0;
}

static int test_ui_init(void) {
    ESP_LOGI(TAG, "Testing UI manager initialization...");
    
    int result = ui_manager_init(480, 272);
    if (result != 0) {
        TEST_FAIL("ui_init", "Failed to initialize UI manager: %d", result);
        return -1;
    }
    
    TEST_PASS("ui_init", "UI manager initialized");
    return 0;
}

static int test_waveform_fps(void) {
    ESP_LOGI(TAG, "Testing waveform rendering performance...");
    
    // Generate some waveform updates and measure FPS
    uint8_t waveform[480];
    audio_player_get_waveform(waveform, 480);
    
    // Run waveform updates for 2 seconds and measure
    int64_t start = esp_timer_get_time();
    int frames = 0;
    
    while (esp_timer_get_time() - start < 2000000) { // 2 seconds
        size_t wave_index = audio_player_get_waveform_index();
        ui_manager_update_waveform(waveform, 480, 0.5f, wave_index);
        ui_manager_process();
        frames++;
        vTaskDelay(pdMS_TO_TICKS(10)); // ~100 FPS target
    }
    
    int64_t elapsed_us = esp_timer_get_time() - start;
    float fps = (float)frames * 1000000.0f / (float)elapsed_us;
    
    // Get detailed performance stats
    uint32_t frame_us, cache_us, draw_us, inv_us;
    waveform_view_get_perf_stats(&frame_us, &cache_us, &draw_us, &inv_us);
    
    TEST_INFO("waveform_fps", "FPS: %.1f, frame: %luus, draw: %luus", 
              fps, (unsigned long)frame_us, (unsigned long)draw_us);
    
    if (fps < 30.0f) {
        TEST_FAIL("waveform_fps", "FPS too low: %.1f (min: 30)", fps);
        return -1;
    }
    
    TEST_PASS("waveform_fps", "Performance OK: %.1f FPS", fps);
    return 0;
}

// ============================================================================
// Controls Tests
// ============================================================================

static int test_controls_init(void) {
    ESP_LOGI(TAG, "Testing controls initialization...");
    
    if (!controls_init(NULL, NULL)) {
        TEST_FAIL("controls_init", "Failed to initialize controls");
        return -1;
    }
    
    TEST_PASS("controls_init", "Controls initialized");
    return 0;
}

// ============================================================================
// Main Test Runner
// ============================================================================

typedef struct {
    const char *name;
    int (*test_fn)(void);
} hw_test_t;

static const hw_test_t storage_tests[] = {
    {"storage_init", test_storage_init},
    {"storage_available", test_storage_available},
};

static const hw_test_t track_db_tests[] = {
    {"track_db_init", test_track_db_init},
    {"track_db_scan", test_track_db_scan},
};

static const hw_test_t audio_tests[] = {
    {"audio_init", test_audio_init},
    {"audio_load", test_audio_load},
    {"audio_duration", test_audio_duration},
    {"audio_play", test_audio_play_pause},
    {"waveform_data", test_waveform_data},
};

static const hw_test_t display_tests[] = {
    {"display_init", test_display_init},
    {"ui_init", test_ui_init},
    {"waveform_fps", test_waveform_fps},
};

static const hw_test_t control_tests[] = {
    {"controls_init", test_controls_init},
};

static int run_test_suite(const char *suite_name, const hw_test_t *tests, 
                          size_t count, int *passed, int *failed) {
    printf("\n--- %s ---\n", suite_name);
    fflush(stdout);
    
    for (size_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "Running test: %s", tests[i].name);
        
        int result = tests[i].test_fn();
        
        if (result == 0) {
            (*passed)++;
        } else {
            (*failed)++;
        }
        
        // Small delay between tests
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return 0;
}

int hw_test_run_all(void) {
    int passed = 0;
    int failed = 0;
    
    printf("\n");
    printf("=== HW_TEST_START ===\n");
    printf("Hardware Test Suite for ESP32 DJ Deck\n");
    fflush(stdout);
    
    // Run test suites in dependency order
    run_test_suite("Storage Tests", storage_tests, 
                   sizeof(storage_tests)/sizeof(storage_tests[0]), &passed, &failed);
    
    run_test_suite("Track Database Tests", track_db_tests,
                   sizeof(track_db_tests)/sizeof(track_db_tests[0]), &passed, &failed);
    
    run_test_suite("Audio Tests", audio_tests,
                   sizeof(audio_tests)/sizeof(audio_tests[0]), &passed, &failed);
    
    run_test_suite("Display/UI Tests", display_tests,
                   sizeof(display_tests)/sizeof(display_tests[0]), &passed, &failed);
    
    run_test_suite("Controls Tests", control_tests,
                   sizeof(control_tests)/sizeof(control_tests[0]), &passed, &failed);
    
    // Cleanup
    audio_player_stop();
    
    printf("\n=== HW_TEST_SUMMARY|passed=%d|failed=%d ===\n", passed, failed);
    fflush(stdout);
    
    ESP_LOGI(TAG, "Tests complete: %d passed, %d failed", passed, failed);
    
    return failed;
}

