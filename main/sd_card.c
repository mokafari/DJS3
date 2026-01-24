/**
 * @file sd_card.c
 * @brief SD card interface implementation using SPI and FATFS
 */

#include "sd_card.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sd_card";
static const char *SD_MOUNT_POINT = "/sdcard";
static bool sd_mounted = false;

static sdmmc_card_t *card = NULL;

bool sd_card_init(void) {
    esp_err_t ret;
    
#ifdef SD_CARD_DISABLE
    ESP_LOGI(TAG, "SD card initialization disabled (SD_CARD_DISABLE defined)");
    return false;
#endif
    
    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "  CS:   GPIO %d", SD_CS_PIN);
    ESP_LOGI(TAG, "  MOSI: GPIO %d", SD_MOSI_PIN);
    ESP_LOGI(TAG, "  MISO: GPIO %d", SD_MISO_PIN);
    ESP_LOGI(TAG, "  SCK:  GPIO %d", SD_SCK_PIN);
    
    // Configure CS pin as output with pull-up and set it high (inactive) before SPI init
    // SD cards require pull-up on CS line (typically 10-50 kOhm)
    gpio_config_t cs_io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << SD_CS_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Enable pull-up for SD card CS
    };
    ret = gpio_config(&cs_io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CS pin: %s", esp_err_to_name(ret));
        return false;
    }
    gpio_set_level(SD_CS_PIN, 1); // Set CS high (inactive)
    ESP_LOGI(TAG, "CS pin configured: GPIO %d (pull-up enabled)", SD_CS_PIN);
    
    // Ensure touch controller CS is held high during SD card initialization
    // Touch controller CS is GPIO 38 (TOUCH_RES from pinout)
    // This prevents bus conflicts during SD card initialization
    // NOTE: If using capacitive touch (GT911), it uses I2C, not SPI, so no conflict
    //       If using resistive touch (XPT2046), it shares SPI3_HOST with SD card
#if defined(TOUCH_XPT2046) && TOUCH_XPT2046
    gpio_config_t touch_cs_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TOUCH_CS_PIN),  // Touch controller CS (GPIO 38)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&touch_cs_conf);  // Ignore errors if already configured
    gpio_set_level(TOUCH_CS_PIN, 1);  // Hold touch CS high (inactive)
    ESP_LOGI(TAG, "Touch controller CS (GPIO %d) held high to avoid bus conflicts", TOUCH_CS_PIN);
#elif defined(TOUCH_GT911) && TOUCH_GT911
    ESP_LOGI(TAG, "Using capacitive touch (GT911 I2C) - no SPI bus conflict with SD card");
#else
    ESP_LOGW(TAG, "Touch controller not configured - assuming no SPI conflict");
#endif
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Longer delay for SD card power stabilization
    
    // SD card shares SPI3_HOST bus with touch controller (XPT2046) if resistive touch is used
    // If capacitive touch (GT911) is used, it uses I2C, so SD card has SPI3_HOST to itself
    // Both use same MOSI/MISO/SCK pins (GPIO 13/11/12) but different CS pins
    // Touch controller CS: GPIO 38, SD card CS: GPIO 10 (TF_CS from pinout)
    // Note: Display uses SPI2_HOST with different pins (GPIO 21/47/45)
    spi_host_device_t spi_host = SPI3_HOST;
    
#if defined(TOUCH_XPT2046) && TOUCH_XPT2046
    ESP_LOGI(TAG, "Touch controller: XPT2046 (SPI) - sharing SPI3_HOST with SD card");
#elif defined(TOUCH_GT911) && TOUCH_GT911
    ESP_LOGI(TAG, "Touch controller: GT911 (I2C) - SD card has exclusive access to SPI3_HOST");
#else
    ESP_LOGI(TAG, "Touch controller: Not configured - SD card has exclusive access to SPI3_HOST");
#endif
    
    // Check if SPI3_HOST is already initialized (by touch controller)
    // If so, we can still add the SD card device to the same bus
    // SPI bus configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Try to initialize SPI bus (may fail if already initialized by touch controller)
    ret = spi_bus_initialize(spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            // SPI bus already initialized (likely by touch controller) - this is OK
            ESP_LOGI(TAG, "SPI3_HOST already initialized (likely by touch controller)");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            ESP_LOGE(TAG, "Error code: 0x%x", ret);
            return false;
        }
    } else {
        ESP_LOGI(TAG, "SPI bus initialized successfully");
    }
    
    // SD card slot configuration
    // Use SDSPI_HOST_DEFAULT() to get all required function pointers initialized
    // Then override only the slot to use SPI3_HOST instead of default SPI2_HOST
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spi_host;  // Override to use SPI3_HOST (shared with touch controller)
    host.max_freq_khz = 200;  // Start with 200kHz for card detection (very low for shared bus and reliability)
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = spi_host;  // Ensure host_id matches slot
    
    ESP_LOGI(TAG, "SD card host configured:");
    ESP_LOGI(TAG, "  Slot requested: SPI3_HOST (value=%d)", spi_host);
    ESP_LOGI(TAG, "  Host.slot actual: %d (SPI2_HOST=1, SPI3_HOST=2)", host.slot);
    ESP_LOGI(TAG, "  CS pin: GPIO %d", SD_CS_PIN);
    ESP_LOGI(TAG, "  Max freq: %d kHz", host.max_freq_khz);
    
    // Verify slot assignment
    // SPI2_HOST = 1, SPI3_HOST = 2
    if (host.slot != spi_host) {
        ESP_LOGE(TAG, "ERROR: Slot mismatch! Requested SPI3_HOST (%d) but got slot %d", spi_host, host.slot);
        ESP_LOGE(TAG, "SPI2_HOST=1, SPI3_HOST=2 - This will cause SD card initialization to fail!");
        return false;
    }
    
    // Additional verification: ensure we're using SPI3_HOST (value 2), not SPI2_HOST (value 1)
    if (host.slot == 1) {
        ESP_LOGE(TAG, "ERROR: Using SPI2_HOST (slot=1) which conflicts with display!");
        ESP_LOGE(TAG, "SD card must use SPI3_HOST (slot=2) to share bus with touch controller");
        return false;
    }
    
    ESP_LOGI(TAG, "✓ Slot verification passed: Using SPI3_HOST (slot=2)");
    
    // FATFS mount options
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    ESP_LOGI(TAG, "Attempting to mount SD card...");
    ESP_LOGI(TAG, "  Mount point: %s", SD_MOUNT_POINT);
    ESP_LOGI(TAG, "  SPI host: SPI3_HOST (slot=%d)", host.slot);
    ESP_LOGI(TAG, "  CS pin: GPIO %d", SD_CS_PIN);
    ESP_LOGI(TAG, "  Clock speed: %d kHz (reduced for initialization)", host.max_freq_khz);
    
    // Mount filesystem
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        
        // For now, treat SD card as optional - don't spam errors
        // User can enable verbose diagnostics by defining SD_CARD_VERBOSE_ERRORS
#ifdef SD_CARD_VERBOSE_ERRORS
        // Print diagnostics for any error
        ESP_LOGI(TAG, "=== SD Card Initialization Diagnostics ===");
        ESP_LOGI(TAG, "  Error code: 0x%x (%s)", ret, esp_err_to_name(ret));
        ESP_LOGI(TAG, "  SPI host: SPI3_HOST (slot=%d)", host.slot);
        ESP_LOGI(TAG, "  Clock speed: %d kHz", host.max_freq_khz);
        int cs_level = gpio_get_level(SD_CS_PIN);
        ESP_LOGI(TAG, "  CS pin (GPIO %d) level: %d (1=high/inactive, 0=low/active)", SD_CS_PIN, cs_level);
#if defined(TOUCH_XPT2046) && TOUCH_XPT2046
        int touch_cs_level = gpio_get_level(TOUCH_CS_PIN);
        ESP_LOGI(TAG, "  Touch CS (GPIO %d) level: %d (should be 1=high/inactive)", TOUCH_CS_PIN, touch_cs_level);
#endif
        
        if (ret == ESP_ERR_INVALID_RESPONSE || ret == ESP_ERR_TIMEOUT || ret == 0x107) {
            ESP_LOGE(TAG, "SD card not responding (0x107)");
            ESP_LOGE(TAG, "Possible causes:");
            ESP_LOGE(TAG, "  1. SD card not properly inserted or not making contact");
            ESP_LOGE(TAG, "  2. Missing external 10kΩ pull-up resistors on CS, MOSI, MISO, CLK lines");
            ESP_LOGE(TAG, "  3. Wrong pin configuration:");
            ESP_LOGE(TAG, "     CS:   GPIO %d (TF_CS)", SD_CS_PIN);
            ESP_LOGE(TAG, "     MOSI: GPIO %d (TF_MOSI/RTP_DIO)", SD_MOSI_PIN);
            ESP_LOGE(TAG, "     MISO: GPIO %d (TF_MISO/RTP_DIN)", SD_MISO_PIN);
            ESP_LOGE(TAG, "     SCK:  GPIO %d (TF_CLK/RTP_CLK)", SD_SCK_PIN);
            ESP_LOGE(TAG, "  4. SD card power issue (check 3.3V supply)");
            ESP_LOGE(TAG, "  5. SPI bus conflict - touch controller CS (GPIO 38) should be held high");
            ESP_LOGE(TAG, "  6. SD card damaged, incompatible, or needs formatting (FAT32)");
            ESP_LOGE(TAG, "  7. Clock speed too high - try reducing max_freq_khz further");
            ESP_LOGE(TAG, "  8. SD card may need longer power-on delay");
        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SD card filesystem");
            ESP_LOGE(TAG, "Possible causes: wrong filesystem format (needs FAT32), or card not initialized");
        } else {
            ESP_LOGE(TAG, "Unexpected error: %s (0x%x)", esp_err_to_name(ret), ret);
        }
        
        ESP_LOGI(TAG, "=== End Diagnostics ===");
#else
        // Silent failure - SD card is optional
        if (ret == ESP_ERR_INVALID_RESPONSE || ret == ESP_ERR_TIMEOUT || ret == 0x107) {
            ESP_LOGW(TAG, "SD card not detected or not responding (0x107) - continuing without SD card");
        } else {
            ESP_LOGW(TAG, "SD card initialization failed: %s (0x%x) - continuing without SD card", esp_err_to_name(ret), ret);
        }
#endif
        
        // Only free SPI bus if we initialized it (not if it was already initialized by touch controller)
        // Note: We can't easily check if we initialized it, so we'll leave it for now
        // The touch controller and SD card can share the same SPI bus
        return false;
    }
    
    
    sd_mounted = true;
    
    // Print card info
    sdmmc_card_print_info(stdout, card);
    
    ESP_LOGI(TAG, "SD card initialized and mounted at %s", SD_MOUNT_POINT);
    return true;
}

void sd_card_deinit(void) {
    if (sd_mounted && card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        sd_mounted = false;
        card = NULL;
        // Free SPI3_HOST bus (shared with touch controller)
        spi_bus_free(SPI3_HOST);
        ESP_LOGI(TAG, "SD card deinitialized");
    }
}

bool sd_card_is_mounted(void) {
    return sd_mounted;
}

const char* sd_card_get_mount_point(void) {
    return sd_mounted ? SD_MOUNT_POINT : NULL;
}

