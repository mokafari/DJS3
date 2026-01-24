/**
 * @file storage.c
 * @brief Unified storage abstraction layer implementation
 */

#include "storage.h"
#include "sd_card.h"
#include "usb_host.h"
#include "esp_log.h"

static const char *TAG = "storage";
static storage_source_t active_source = STORAGE_SOURCE_NONE;

bool storage_init(void) {
    ESP_LOGI(TAG, "Initializing storage abstraction layer");
    
    // Initialize SD card
    if (!sd_card_init()) {
        ESP_LOGW(TAG, "SD card initialization failed");
    }
    
    // Initialize USB host
    if (!usb_host_init(NULL, NULL)) {
        ESP_LOGW(TAG, "USB host initialization failed");
    }
    
    // Auto-select storage source
    return storage_auto_select();
}

void storage_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing storage abstraction layer");
    
    sd_card_deinit();
    usb_host_deinit();
    
    active_source = STORAGE_SOURCE_NONE;
}

storage_source_t storage_get_active_source(void) {
    return active_source;
}

const char* storage_get_mount_point(void) {
    switch (active_source) {
        case STORAGE_SOURCE_SD_CARD:
            return sd_card_get_mount_point();
        case STORAGE_SOURCE_USB:
            return usb_host_get_mount_point();
        default:
            return NULL;
    }
}

bool storage_is_available(void) {
    return active_source != STORAGE_SOURCE_NONE;
}

bool storage_auto_select(void) {
    // Priority: USB > SD Card
    if (usb_host_is_mounted()) {
        active_source = STORAGE_SOURCE_USB;
        ESP_LOGI(TAG, "Selected USB as storage source");
        return true;
    } else if (sd_card_is_mounted()) {
        active_source = STORAGE_SOURCE_SD_CARD;
        ESP_LOGI(TAG, "Selected SD card as storage source");
        return true;
    }
    
    active_source = STORAGE_SOURCE_NONE;
    ESP_LOGW(TAG, "No storage source available");
    return false;
}

