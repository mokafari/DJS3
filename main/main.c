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

static const char *TAG = "main";

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

/**
 * @brief Button event callback
 */
static void button_event_handler(button_id_t button, bool pressed, void *arg) {
    (void)arg;
    
    if (!pressed) {
        return; // Only handle press events
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
        case BUTTON_HOT_CUE_1:
        case BUTTON_HOT_CUE_2:
        case BUTTON_HOT_CUE_3:
        case BUTTON_HOT_CUE_4:
            ESP_LOGI(TAG, "Hot Cue %d pressed", button - BUTTON_HOT_CUE_1 + 1);
            {
                uint32_t cue_pos = cue_points_get(button - BUTTON_HOT_CUE_1);
                if (cue_pos > 0) {
                    audio_player_seek(cue_pos);
                } else {
                    cue_points_set(button - BUTTON_HOT_CUE_1, audio_player_get_position());
                }
            }
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
        ESP_LOGI(TAG, "Pitch control init complete");
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
        }
    } else {
        ESP_LOGI(TAG, "Skipping UI initialization (display not available)");
    }

    // Initialize track database (non-critical)
    ESP_LOGI(TAG, "Initializing track database...");
    if (!track_db_init()) {
        ESP_LOGW(TAG, "Track database initialization failed - continuing anyway");
    } else {
        ESP_LOGI(TAG, "Track database init complete");
        
            // Scan for tracks
            ESP_LOGI(TAG, "Scanning for MP3 tracks...");
            uint32_t track_count = track_db_scan();
            ESP_LOGI(TAG, "Found %lu tracks", track_count);
            
            // Auto-play first track if available
            if (track_count > 0) {
                track_info_t info;
                if (track_db_get_track(0, &info)) {
                    ESP_LOGI(TAG, "Auto-playing first track: %s", info.filename);
                    
                    // Build full path (assuming storage mount point is known, usually /sdcard)
                    // track_db stores relative path or filename, ensure we have full path
                    // The track_db_scan implementation stores the filename relative to the scan root
                    // But audio_player_load needs a full path if not handled internally
                    // Let's assume the track_info contains the filename relative to root, 
                    // so we prepend /sdcard/ if needed.
                    // Wait, track_db stores just filename. We need to construct path.
                    
                    static char full_path[512];
                    snprintf(full_path, sizeof(full_path), "/%s", info.filename);
                    
                    if (audio_player_load(full_path)) {
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
    while (1) {
        loop_count++;
        if (loop_count % 1000 == 0) {
            ESP_LOGI(TAG, "Main loop running... (iteration %lu)", loop_count);
        }
        
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Update controls (buttons, encoders)
        controls_update();
        
        // Update pitch control
        pitch_control_update();
        
        // Update audio player
        audio_player_update();
        
        // Process UI (call every loop iteration for responsive UI)
        // Only if display and UI are initialized
        if (display_is_initialized()) {
            ui_manager_process();
        }
        
        // Update UI with current state (every 50ms for smooth updates)
        if (now - last_update >= 50) {
            // Only update UI if display is initialized
            if (display_is_initialized()) {
                if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
                uint32_t pos = audio_player_get_position();
                uint32_t dur = audio_player_get_duration();
                float position = dur > 0 ? (float)pos / (float)dur : 0.0f;
                
                // Update waveform in UI
                // TODO: Get actual waveform data from audio player/FFT
                // For now, use placeholder data
                static uint8_t waveform_data[480];
                for (int i = 0; i < 480; i++) {
                    waveform_data[i] = (uint8_t)(128 + 127 * sin(i * 0.1f + now * 0.001f));
                }
                ui_manager_update_waveform(waveform_data, 480, position);
                
                // Update telemetry (BPM, pitch, phase error)
                // TODO: Get actual BPM from track_db or audio analysis
                float bpm = 120.0f; // Placeholder
                // TODO: Get actual pitch percentage from pitch_control
                float pitch = 0.0f; // Placeholder
                // TODO: Calculate phase error from sync system
                float phase_error = 0.0f; // Placeholder
                ui_manager_update_telemetry(bpm, pitch, phase_error);
                
                // Update metadata
                // TODO: Get actual title and key from track_db
                const char *title = "Track Playing"; // Placeholder
                const char *key = "4A"; // Placeholder
                int32_t time_remaining = dur > pos ? (int32_t)(dur - pos) : -(int32_t)pos;
                ui_manager_update_metadata(title, key, time_remaining);
                } else {
                    // Clear UI when not playing
                    ui_manager_update_metadata("No Track", "--", 0);
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

