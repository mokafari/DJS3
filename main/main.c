/**
 * @file main.c
 * @brief CDJ-style DJ deck application for ESP32-S3
 * 
 * Main application orchestrating all DJ deck components:
 * - Audio output (PCM5102A DAC via I2S)
 * - Storage (USB OTG + SD card)
 * - Audio playback (MP3)
 * - DJ controls (buttons, jog wheel, pitch)
 * - Cue points and loops
 * - Display and waveform
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_psram.h"
#include "board_config.h"
#include "audio_output.h"
#include "storage.h"
#include "audio_player.h"
#include "controls.h"
#include "pitch_control.h"
#include "cue_points.h"
#include "loop_control.h"
#include "display.h"
#include "waveform.h"
#include "track_db.h"
#include "ui_manager.h"
#include "metadata.h"
#include "analyzer.h"
#include "library_db.h"
#include "preferences.h"
#include "track_history.h"
#include "dsp_pipeline.h"
#include "slip_mode.h"
#include "jog_wheel.h"
#include "led_controller.h"
#include "ext_controller.h"
#include "auto_dj.h"
#include "recorder.h"
#include "track_prep.h"
#include "search_view.h"

#ifdef CONFIG_HW_TEST_MODE
#include "hw_test_harness.h"
#endif

static const char *TAG = "main";

// Global DSP pipeline instance
static dsp_pipeline_t g_dsp_pipeline;

/**
 * @brief Initialize NVS (Non-Volatile Storage)
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
    return ret;
}

/**
 * @brief Print board information
 */
static void print_board_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Board: %s", BOARD_NAME);
    ESP_LOGI(TAG, "MCU: %s", BOARD_MCU);
    ESP_LOGI(TAG, "CPU Frequency: %d MHz", BOARD_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "Chip: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "Chip revision: %d", chip_info.revision);
    ESP_LOGI(TAG, "CPU cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Silicon revision: %d", chip_info.revision);
    
    // Flash size from configuration
    ESP_LOGI(TAG, "Flash: %dMB %s", BOARD_FLASH_SIZE_MB,
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    ESP_LOGI(TAG, "========================================");
}

/**
 * @brief Print pin configuration
 */
static void print_pin_config(void)
{
    ESP_LOGI(TAG, "Pin Configuration:");
    ESP_LOGI(TAG, "  Display CS:    GPIO %d", DISPLAY_CS_PIN);
    ESP_LOGI(TAG, "  Display SCK:   GPIO %d", DISPLAY_SCK_PIN);
    ESP_LOGI(TAG, "  Display D0-D3: GPIO %d, %d, %d, %d", 
             DISPLAY_D0_PIN, DISPLAY_D1_PIN, DISPLAY_D2_PIN, DISPLAY_D3_PIN);
    ESP_LOGI(TAG, "  Backlight:     GPIO %d (PWM)", DISPLAY_BL_PIN);
    
#if defined(TOUCH_XPT2046) && TOUCH_XPT2046
    ESP_LOGI(TAG, "  Touch (XPT2046):");
    ESP_LOGI(TAG, "    SCK:  GPIO %d", TOUCH_SCK_PIN);
    ESP_LOGI(TAG, "    MISO: GPIO %d", TOUCH_MISO_PIN);
    ESP_LOGI(TAG, "    MOSI: GPIO %d", TOUCH_MOSI_PIN);
    ESP_LOGI(TAG, "    CS:   GPIO %d", TOUCH_CS_PIN);
    ESP_LOGI(TAG, "    INT:  GPIO %d", TOUCH_INT_PIN);
#elif defined(TOUCH_GT911) && TOUCH_GT911
    ESP_LOGI(TAG, "  Touch (GT911):");
    ESP_LOGI(TAG, "    SCL:  GPIO %d", TOUCH_SCL_PIN);
    ESP_LOGI(TAG, "    SDA:  GPIO %d", TOUCH_SDA_PIN);
    ESP_LOGI(TAG, "    RES:  GPIO %d", TOUCH_RES_PIN);
    ESP_LOGI(TAG, "    INT:  GPIO %d", TOUCH_INT_PIN);
#endif
}

/**
 * @brief Check and print PSRAM information
 */
static void check_psram(void)
{
    if (esp_psram_get_size() > 0) {
        size_t psram_size = esp_psram_get_size();
        ESP_LOGI(TAG, "PSRAM detected: %d MB (%d bytes)", 
                 psram_size / (1024 * 1024), psram_size);
        
        if (psram_size != BOARD_PSRAM_SIZE_MB * 1024 * 1024) {
            ESP_LOGW(TAG, "PSRAM size mismatch! Expected %d MB, got %d MB",
                     BOARD_PSRAM_SIZE_MB, psram_size / (1024 * 1024));
        }
        
        // Test PSRAM allocation
        void *test_ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
        if (test_ptr) {
            ESP_LOGI(TAG, "PSRAM allocation test: SUCCESS (allocated 1KB at %p)", test_ptr);
            heap_caps_free(test_ptr);
        } else {
            ESP_LOGE(TAG, "PSRAM allocation test: FAILED");
        }
    } else {
        ESP_LOGE(TAG, "PSRAM NOT detected!");
    }
}

/**
 * @brief Print memory information
 */
static void print_memory_info(void)
{
    ESP_LOGI(TAG, "Memory Information:");
    ESP_LOGI(TAG, "  Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Largest free block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "  Minimum free heap: %d bytes", esp_get_minimum_free_heap_size());
    
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "  Internal RAM:");
    ESP_LOGI(TAG, "    Total: %d bytes", heap_info.total_free_bytes + heap_info.total_allocated_bytes);
    ESP_LOGI(TAG, "    Free: %d bytes", heap_info.total_free_bytes);
    ESP_LOGI(TAG, "    Allocated: %d bytes", heap_info.total_allocated_bytes);
    
    if (esp_psram_get_size() > 0) {
        heap_caps_get_info(&heap_info, MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "  PSRAM:");
        ESP_LOGI(TAG, "    Total: %d bytes", heap_info.total_free_bytes + heap_info.total_allocated_bytes);
        ESP_LOGI(TAG, "    Free: %d bytes", heap_info.total_free_bytes);
        ESP_LOGI(TAG, "    Allocated: %d bytes", heap_info.total_allocated_bytes);
    }
}

/**
 * @brief Initialize LEDC for backlight PWM control
 */
static esp_err_t init_backlight(void)
{
    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_BIT,
        .freq_hz          = LEDC_BASE_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_BACKLIGHT,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = DISPLAY_BL_PIN,
        .duty           = BRIGHTNESS_TO_DUTY(LEDC_DEFAULT_BRIGHTNESS),
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Backlight initialized: GPIO %d, brightness %d/255", 
             DISPLAY_BL_PIN, LEDC_DEFAULT_BRIGHTNESS);
    return ESP_OK;
}

/**
 * @brief Test GPIO functionality (toggle display CS pin)
 */
static void test_gpio(void)
{
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << DISPLAY_CS_PIN),
        .pull_down_en = 0,
        .pull_up_en   = 0,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Testing GPIO %d (Display CS)...", DISPLAY_CS_PIN);
    
    for (int i = 0; i < 5; i++) {
        gpio_set_level(DISPLAY_CS_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(DISPLAY_CS_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    gpio_set_level(DISPLAY_CS_PIN, 0); // Set CS low (active)
    ESP_LOGI(TAG, "GPIO test completed");
}

// ============================================================================
// Hot Cue Long-Press Detection
// ============================================================================

#define HOT_CUE_LONG_PRESS_MS  500  // Hold for 500ms to delete

static uint32_t hot_cue_press_time[8] = {0};
static bool hot_cue_triggered[8] = {false};

/**
 * @brief Check if button is a hot cue button
 */
static inline bool is_hot_cue_button(button_id_t button) {
    return button >= BUTTON_HOT_CUE_1 && button <= BUTTON_HOT_CUE_8;
}

/**
 * @brief Get hot cue index from button
 */
static inline int get_hot_cue_index(button_id_t button) {
    return button - BUTTON_HOT_CUE_1;
}

/**
 * @brief Handle hot cue button press
 */
static void handle_hot_cue_press(int cue_index) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    hot_cue_press_time[cue_index] = now;
    hot_cue_triggered[cue_index] = false;
    
    // Get current position in milliseconds
    uint32_t current_pos_ms = audio_player_get_position_ms();
    
    // Trigger the hot cue
    uint32_t cue_pos_ms = cue_points_trigger(cue_index, current_pos_ms);
    
    if (cue_pos_ms > 0) {
        // Cue exists - seek to it
        // Convert ms to seconds for audio_player_seek (legacy API)
        audio_player_seek(cue_pos_ms / 1000);
        
        // Start playback if in PLAY mode
        cue_trigger_mode_t mode = cue_points_get_trigger_mode();
        if (mode == CUE_MODE_PLAY || mode == CUE_MODE_PREVIEW) {
            if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING) {
                audio_player_play();
            }
        }
        
        hot_cue_triggered[cue_index] = true;
        ESP_LOGI(TAG, "Hot Cue %d triggered -> %u ms", cue_index + 1, cue_pos_ms);
    } else {
        // Cue was set at current position
        ESP_LOGI(TAG, "Hot Cue %d set at %u ms", cue_index + 1, current_pos_ms);
    }
}

/**
 * @brief Handle hot cue button release
 */
static void handle_hot_cue_release(int cue_index) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t press_duration = now - hot_cue_press_time[cue_index];
    
    // Check for long press -> delete
    if (press_duration >= HOT_CUE_LONG_PRESS_MS && hot_cue_triggered[cue_index]) {
        if (cue_points_delete(cue_index)) {
            ESP_LOGI(TAG, "Hot Cue %d DELETED (long press %u ms)", 
                     cue_index + 1, press_duration);
        }
    }
    
    // Handle preview mode release
    cue_points_release(cue_index);
    
    // If in preview mode and was triggered, pause on release
    if (cue_points_get_trigger_mode() == CUE_MODE_PREVIEW && hot_cue_triggered[cue_index]) {
        audio_player_pause();
    }
    
    hot_cue_triggered[cue_index] = false;
}

/**
 * @brief Button event callback
 */
static void button_event_handler(button_id_t button, bool pressed, void *arg) {
    (void)arg;
    
    // Handle hot cue buttons specially (need both press and release)
    if (is_hot_cue_button(button)) {
        int cue_index = get_hot_cue_index(button);
        if (pressed) {
            handle_hot_cue_press(cue_index);
        } else {
            handle_hot_cue_release(cue_index);
        }
        return;
    }
    
    // Other buttons: only handle press events
    if (!pressed) {
        return;
    }
    
    switch (button) {
        case BUTTON_CUE:
            ESP_LOGI(TAG, "Cue button pressed");
            // TODO: Jump to cue point or set cue point
            break;
        case BUTTON_PLAY_PAUSE:
            ESP_LOGI(TAG, "Play/Pause button pressed");
            if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
                audio_player_pause();
            } else {
                audio_player_play();
            }
            break;
        case BUTTON_SYNC:
            ESP_LOGI(TAG, "Sync button pressed");
            // TODO: Implement sync functionality
            break;
        case BUTTON_LOOP_IN:
            ESP_LOGI(TAG, "Loop In button pressed");
            loop_control_set_in(audio_player_get_position());
            break;
        case BUTTON_LOOP_OUT:
            ESP_LOGI(TAG, "Loop Out button pressed");
            loop_control_set_out(audio_player_get_position());
            break;
        default:
            break;
    }
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    // CRITICAL: Early output to verify boot
    printf("\n\n");
    printf("========================================\n");
    printf("ESP32-S3 CDJ DJ Deck - BOOT START\n");
    printf("========================================\n");
    fflush(stdout);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 CDJ DJ Deck");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    init_nvs();
    ESP_LOGI(TAG, "NVS init complete");

    // Print board information
    print_board_info();
    print_pin_config();

    // Check PSRAM
    ESP_LOGI(TAG, "Checking PSRAM...");
    check_psram();
    ESP_LOGI(TAG, "PSRAM check complete");

    // Print memory information
    print_memory_info();
    ESP_LOGI(TAG, "Memory info printed");

#ifdef CONFIG_HW_TEST_MODE
    // ========================================
    // HARDWARE TEST MODE
    // ========================================
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "=== HARDWARE TEST MODE ENABLED ===");
    ESP_LOGI(TAG, "========================================");
    printf("\n*** HARDWARE TEST MODE ***\n");
    fflush(stdout);
    
    // Run hardware tests
    int test_failures = hw_test_run_all();
    
    ESP_LOGI(TAG, "========================================");
    if (test_failures == 0) {
        ESP_LOGI(TAG, "ALL HARDWARE TESTS PASSED");
    } else {
        ESP_LOGE(TAG, "HARDWARE TESTS FAILED: %d failures", test_failures);
    }
    ESP_LOGI(TAG, "========================================");
    
    // Halt - dev.py will capture the output
    printf("\n*** TEST MODE COMPLETE - HALTING ***\n");
    fflush(stdout);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    // Initialize backlight
    ESP_LOGI(TAG, "Initializing backlight...");
    init_backlight();
    ESP_LOGI(TAG, "Backlight init complete");

    // Initialize storage (non-critical, continue on failure)
    ESP_LOGI(TAG, "Initializing storage...");
    if (!storage_init()) {
        ESP_LOGW(TAG, "Storage initialization failed - continuing anyway");
    } else {
        ESP_LOGI(TAG, "Storage init complete");
    }

    // Initialize preferences system (depends on NVS)
    ESP_LOGI(TAG, "Initializing preferences system...");
    if (!prefs_init()) {
        ESP_LOGW(TAG, "Preferences initialization failed - using defaults");
    } else {
        // Pre-load audio and display preferences for fast access during playback
        prefs_cache_preload(PREFS_CAT_AUDIO);
        prefs_cache_preload(PREFS_CAT_DISPLAY);
        prefs_cache_preload(PREFS_CAT_CONTROLS);
        ESP_LOGI(TAG, "Preferences initialized and cached");
    }

    // Initialize DSP pipeline (standalone, no dependencies)
    ESP_LOGI(TAG, "Initializing DSP pipeline...");
    if (!dsp_pipeline_init(&g_dsp_pipeline, 44100)) {
        ESP_LOGW(TAG, "DSP pipeline initialization failed - effects unavailable");
    } else {
        // Configure default effect chain: EQ -> Filter -> Limiter
        dsp_effect_config_t eq_cfg;
        dsp_eq_default(&eq_cfg);
        dsp_pipeline_add_effect(&g_dsp_pipeline, &eq_cfg);
        
        // Enable master limiter to prevent clipping
        dsp_master_limiter_t limiter_cfg = {
            .enabled = true,
            .threshold = 0.95f,
            .ceiling = 0.99f,
            .release_ms = 50.0f
        };
        dsp_pipeline_set_master_limiter(&g_dsp_pipeline, &limiter_cfg);
        ESP_LOGI(TAG, "DSP pipeline initialized with EQ and limiter");
    }

    // Initialize slip mode (standalone)
    ESP_LOGI(TAG, "Initializing slip mode...");
    slip_mode_init();
    slip_mode_set_sample_rate(44100);
    slip_mode_set_crossfade(50);  // 50ms crossfade on snap-back
    ESP_LOGI(TAG, "Slip mode initialized");

    // Initialize OpenDeck metadata system
    ESP_LOGI(TAG, "Initializing metadata system...");
    metadata_init();
    ESP_LOGI(TAG, "Metadata system initialized");
    
    // Initialize library database (fast load from library.db)
    ESP_LOGI(TAG, "Loading library database...");
    library_db_init();
    if (library_db_load()) {
        ESP_LOGI(TAG, "Library loaded: %u entries", library_db_get_count());
        // Start background verification
        library_db_verify_async();
    } else {
        ESP_LOGW(TAG, "No library.db found - will rebuild on first scan");
    }
    
    // Initialize track history (depends on storage)
    ESP_LOGI(TAG, "Initializing track history...");
    if (!track_history_init()) {
        ESP_LOGW(TAG, "Track history initialization failed - history tracking disabled");
    } else {
        ESP_LOGI(TAG, "Track history initialized: %lu unique tracks, %lu total plays",
                 track_history_get_total_tracks(), track_history_get_total_plays());
    }

    // Initialize background analyzer
    ESP_LOGI(TAG, "Initializing analyzer...");
    analyzer_init();
    ESP_LOGI(TAG, "Analyzer initialized");

    // Initialize track prep system
    ESP_LOGI(TAG, "Initializing track prep system...");
    if (!track_prep_init()) {
        ESP_LOGW(TAG, "Track prep initialization failed - batch analysis unavailable");
    } else {
        ESP_LOGI(TAG, "Track prep system initialized");
    }

    // Initialize audio output (non-critical)
    ESP_LOGI(TAG, "Initializing audio output...");
    if (!audio_output_init()) {
        ESP_LOGW(TAG, "Audio output initialization failed - continuing anyway");
    } else {
        ESP_LOGI(TAG, "Audio output init complete");
    }

    // Initialize audio player (non-critical)
    ESP_LOGI(TAG, "Initializing audio player...");
    fflush(stdout);
    if (!audio_player_init()) {
        ESP_LOGW(TAG, "Audio player initialization failed - continuing anyway");
        fflush(stdout);
    } else {
        ESP_LOGI(TAG, "Audio player init complete");
        fflush(stdout);
    }
    
    ESP_LOGI(TAG, "Continuing after audio player init...");
    fflush(stdout);

    // Initialize controls (non-critical)
    ESP_LOGI(TAG, "Initializing controls...");
    if (!controls_init(button_event_handler, NULL)) {
        ESP_LOGW(TAG, "Controls initialization failed - continuing anyway");
    } else {
        ESP_LOGI(TAG, "Controls init complete");
    }

    // Initialize pitch control (non-critical)
    ESP_LOGI(TAG, "Initializing pitch control...");
    if (!pitch_control_init()) {
        ESP_LOGW(TAG, "Pitch control initialization failed - continuing anyway");
    } else {
        // Apply pitch range from preferences
        int32_t pitch_range = prefs_get_pitch_range();
        ESP_LOGI(TAG, "Pitch control init complete (range: +/-%ld%%)", (long)pitch_range);
    }

    // Initialize jog wheel controller (after controls)
    ESP_LOGI(TAG, "Initializing jog wheel controller...");
    jog_config_t jog_cfg = jog_wheel_get_default_config();
    jog_cfg.sensitivity.scratch_sensitivity = 1.0f;
    jog_cfg.sensitivity.nudge_sensitivity = 1.0f;
    // Apply jog sensitivity from preferences
    jog_cfg.sensitivity.scratch_sensitivity = (float)prefs_get_jog_sensitivity() / 5.0f;
    jog_cfg.sensitivity.nudge_sensitivity = (float)prefs_get_jog_sensitivity() / 5.0f;
    jog_cfg.slip_mode_integration = true;  // Auto-trigger slip on scratch
    if (!jog_wheel_init(&jog_cfg)) {
        ESP_LOGW(TAG, "Jog wheel initialization failed - continuing anyway");
    } else {
        ESP_LOGI(TAG, "Jog wheel controller initialized");
    }

    // Initialize LED controller (after display)
    ESP_LOGI(TAG, "Initializing LED controller...");
    if (!led_controller_init()) {
        ESP_LOGW(TAG, "LED controller initialization failed - continuing anyway");
    } else {
        // Apply brightness from preferences
        led_controller_set_brightness(prefs_get_brightness());
        ESP_LOGI(TAG, "LED controller initialized");
    }

    // Initialize external controller system (HID/MIDI)
    ESP_LOGI(TAG, "Initializing external controller system...");
    if (!ext_ctrl_init()) {
        ESP_LOGW(TAG, "External controller initialization failed - HID/MIDI disabled");
    } else {
        ESP_LOGI(TAG, "External controller system initialized");
    }

    // Initialize master output recorder (depends on storage)
    ESP_LOGI(TAG, "Initializing master recorder...");
    if (!recorder_init()) {
        ESP_LOGW(TAG, "Recorder initialization failed - recording unavailable");
    } else {
        ESP_LOGI(TAG, "Master recorder initialized");
    }

          // Initialize display (required for UI)
          ESP_LOGI(TAG, "Initializing display...");
          bool display_ok = display_init();
          if (!display_ok) {
              ESP_LOGW(TAG, "Display initialization failed - UI will not work");
          } else {
              ESP_LOGI(TAG, "Display init complete");
              
              // Enable SPI logging for debugging
              display_enable_spi_logging(false);
              
              // Run byte-order test
              display_test_byte_order();
              
              // Draw test pattern: color bars (comment out if not needed)
              // display_test_color_bars();
          }

        // Initialize track database (non-critical)

        ESP_LOGI(TAG, "Initializing track database...");

        bool track_db_ok = false;

        if (!track_db_init()) {

            ESP_LOGW(TAG, "Track database initialization failed - continuing anyway");

        } else {

            ESP_LOGI(TAG, "Track database init complete");

            

            // Scan for tracks immediately so UI can show them

            ESP_LOGI(TAG, "Scanning for MP3 tracks...");

            uint32_t track_count = track_db_scan();

            ESP_LOGI(TAG, "Found %lu tracks", track_count);

            track_db_ok = true;

        }

    

        // Initialize Auto-DJ system (depends on track database)
        ESP_LOGI(TAG, "Initializing Auto-DJ system...");
        if (!auto_dj_init()) {
            ESP_LOGW(TAG, "Auto-DJ initialization failed - auto-mixing unavailable");
        } else {
            // Load configuration from preferences
            auto_dj_config_t adj_config;
            auto_dj_get_default_config(&adj_config);
            auto_dj_set_config(&adj_config);
            ESP_LOGI(TAG, "Auto-DJ system initialized");
        }

        // Initialize UI system (only if display is initialized)
        if (display_ok) {
            ESP_LOGI(TAG, "Initializing UI system...");

            ESP_LOGI(TAG, "UI dimensions: %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);

            ESP_LOGI(TAG, "Calling ui_manager_init...");

            if (ui_manager_init(DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0) {

                ESP_LOGW(TAG, "UI initialization failed - continuing anyway");

                display_ok = false; // Mark display as not available for UI

            } else {

                ESP_LOGI(TAG, "UI system initialized successfully");

                // Set default theme

                ESP_LOGI(TAG, "Setting UI theme to AMBER...");

                ui_manager_set_theme(UI_THEME_AMBER);

                ESP_LOGI(TAG, "UI theme set");

                // Refresh crate view with scanned tracks
                if (track_db_ok && track_db_get_count() > 0) {
                    ESP_LOGI(TAG, "Refreshing crate view with %lu tracks", track_db_get_count());
                    ui_manager_refresh_crate();
                }

            }

        } else {

            ESP_LOGI(TAG, "Skipping UI initialization (display not available)");

        }

    

        // Auto-play logic

        if (track_db_ok) {

            uint32_t track_count = track_db_get_count();

            if (track_count > 0) {

                track_info_t info;

                if (track_db_get_track(0, &info)) {

                    ESP_LOGI(TAG, "Auto-playing first track: %s", info.filename);

                    

                    if (audio_player_load(info.filename)) {

                        audio_player_play();

                    } else {

                        ESP_LOGE(TAG, "Failed to load auto-play track");

                    }

                }

            } else {

                ESP_LOGW(TAG, "No tracks found to auto-play");

            }

        }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "DJ Deck initialization complete!");
    ESP_LOGI(TAG, "System ready for operation.");
    ESP_LOGI(TAG, "========================================");
    printf("BOOT COMPLETE - Entering main loop\n");
    fflush(stdout);

    // Main loop
    uint32_t last_update = 0;
    uint32_t loop_count = 0;
    audio_player_state_t last_state = AUDIO_PLAYER_STATE_STOPPED;
    bool overview_loaded = false;  // Track if overview waveform is loaded for current track
    
    while (1) {
        loop_count++;
        
        // Debug: Log audio player state changes and periodic status
        audio_player_state_t current_state = audio_player_get_state();
        if (current_state != last_state) {
            const char* state_names[] = {"STOPPED", "PLAYING", "PAUSED", "LOADING"};
            ESP_LOGI(TAG, "Audio player state changed: %s -> %s", 
                     state_names[last_state], state_names[current_state]);
            
            // Reset overview flag when track stops (new track will need new overview)
            if (current_state == AUDIO_PLAYER_STATE_STOPPED) {
                overview_loaded = false;
            }
            
            last_state = current_state;
        }
        
        if (loop_count % 100 == 0) {
            const char* state_names[] = {"STOPPED", "PLAYING", "PAUSED", "LOADING"};
            uint32_t pos = audio_player_get_position();
            uint32_t dur = audio_player_get_duration();
            float pitch = pitch_control_get();
            ESP_LOGI(TAG, "Main loop #%lu | Audio: %s | Pos: %lus/%lus | Pitch: %.1f%%", 
                     loop_count, state_names[current_state], pos, dur, pitch);
        }
        
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Update controls (buttons, encoders)
        controls_update();
        
        // Update jog wheel controller
        jog_wheel_update();
        
        // Update pitch control
        pitch_control_update();
        
        // Update slip mode background timeline
        slip_mode_update();
        
        // Update LED controller (animations, beat sync)
        led_controller_update();
        
        // Update Auto-DJ (if enabled)
        if (auto_dj_is_enabled()) {
            auto_dj_update();
        }
        
        // Update audio player
        audio_player_update();
        
        // Process UI (call every loop iteration for responsive UI)
        // Only if display and UI are initialized
        if (display_is_initialized()) {
            ui_manager_process();
        }
        
        // Update UI with current state (every 16ms = ~60 FPS target)
        if (now - last_update >= 16) {
            // Only update UI if display is initialized
            if (display_is_initialized()) {
                if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
                uint32_t pos = audio_player_get_position();
                uint32_t dur = audio_player_get_duration();
                float position = dur > 0 ? (float)pos / (float)dur : 0.0f;
                float precise_time = audio_player_get_precise_position();
                
                // Load overview waveform once metadata is available
                if (!overview_loaded && audio_player_has_metadata()) {
                    static uint8_t overview_buf[480];
                    if (audio_player_get_overview(overview_buf, 480)) {
                        ui_manager_set_overview_waveform(overview_buf, 480);
                        overview_loaded = true;
                        ESP_LOGI(TAG, "Overview waveform loaded from metadata");
                    }
                }
                
                // Update waveform in UI with real data
                static uint8_t waveform_data[480];
                audio_player_get_waveform(waveform_data, 480);
                size_t wave_index = audio_player_get_waveform_index();
                ui_manager_update_waveform(waveform_data, 480, position, precise_time, wave_index);
                
                // Update telemetry (BPM, pitch, phase error)
                float bpm = audio_player_get_bpm();
                if (bpm < 1.0f) bpm = 120.0f;  // Default if not analyzed
                float pitch_val = pitch_control_get();
                float phase_error = 0.0f;
                ui_manager_update_telemetry(bpm, pitch_val, phase_error);
                
                // Update metadata
                const char *title = "Track Playing";
                if (track_db_get_count() > 0) {
                    title = audio_player_get_track_title();
                }
                
                // Get key from metadata
                const char *key = audio_player_get_key_name();
                ui_manager_update_metadata(title, key, pos, dur);
                } else {
                    // Clear UI when not playing
                    ui_manager_update_metadata("No Track", "--", 0, 0);
                    ui_manager_update_telemetry(0.0f, 0.0f, 0.0f);
                }
            }
            
            last_update = now;
        }
        
        // Handle jog wheel
        int8_t jog_delta = controls_get_jog_delta();
        if (jog_delta != 0) {
            bool touched = controls_get_jog_touch();
            if (touched) {
                // Scratch mode
                ESP_LOGI(TAG, "Jog wheel scratch: %d", jog_delta);
                // TODO: Implement scratch functionality
            } else {
                // Pitch bend or nudge
                ESP_LOGI(TAG, "Jog wheel nudge: %d", jog_delta);
                // TODO: Implement nudge functionality
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

