/**
 * @file usb_host.c
 * @brief USB OTG host mode implementation for reading USB sticks
 */

#include "usb_host.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "esp_vfs_fat.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
// Note: ESP-IDF doesn't include a built-in USB MSC host driver
// This requires implementing MSC protocol or using a third-party library
// For now, device detection is implemented, mounting requires MSC driver
#include <string.h>

static const char *TAG = "usb_host";
static const char *USB_MOUNT_POINT = "/usb";

static usb_host_client_handle_t client_handle;
static usb_device_handle_t device_handle;
static bool usb_mounted = false;
static usb_host_event_cb_t event_callback = NULL;
static void *event_callback_arg = NULL;
static TaskHandle_t usb_host_task_handle = NULL;

// USB MSC client handle
static usb_host_client_handle_t msc_client_handle = NULL;

/**
 * @brief USB host library task
 */
static void usb_host_lib_task(void *arg) {
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All devices freed");
        }
    }
}

/**
 * @brief Open USB device and claim interface
 * TODO: Implement proper USB device opening and interface claiming
 * This requires understanding the ESP-IDF USB host API structure for config descriptors
 */
#if 0
static esp_err_t usb_open_device(usb_device_handle_t dev_hdl) {
    esp_err_t ret;
    const usb_device_desc_t *device_desc;
    
    ret = usb_host_get_device_descriptor(dev_hdl, &device_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device descriptor");
        return ret;
    }
    
    ESP_LOGI(TAG, "Device: VID=0x%04x PID=0x%04x", device_desc->idVendor, device_desc->idProduct);
    
    // TODO: Implement proper interface enumeration using usb_host_get_active_config_descriptor
    // and parsing the configuration descriptor structure correctly
    
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

/**
 * @brief Mount USB MSC device as FATFS
 * TODO: Implement USB MSC mounting using TinyUSB MSC host or custom MSC driver
 */
#if 0
static esp_err_t usb_mount_msc(usb_device_handle_t dev_hdl) {
    // TODO: Use TinyUSB MSC host or implement custom MSC driver
    // For now, this is a placeholder
    ESP_LOGW(TAG, "USB MSC mounting requires TinyUSB MSC host implementation");
    ESP_LOGW(TAG, "Mount point: %s", USB_MOUNT_POINT);
    
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

/**
 * @brief USB device event callback
 */
static void usb_device_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    (void)arg; // Unused parameter
    
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "New device connected (address: %d)", event_msg->new_dev.address);
            // Open the device
            // Note: In ESP-IDF v5.5, we need to open the device using usb_host_device_open
            // For now, we'll just store the address and notify
            // TODO: Implement proper device opening and MSC mounting
            if (event_callback) {
                event_callback(USB_HOST_EVENT_DEVICE_CONNECTED, event_callback_arg);
            }
            break;
            
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "Device disconnected");
            if (usb_mounted) {
                // Unmount filesystem
                esp_vfs_fat_unregister_path(USB_MOUNT_POINT);
                usb_mounted = false;
                if (event_callback) {
                    event_callback(USB_HOST_EVENT_UNMOUNTED, event_callback_arg);
                }
                ESP_LOGI(TAG, "USB device unmounted");
            }
            if (device_handle) {
                usb_host_device_close(client_handle, device_handle);
                device_handle = NULL;
            }
            if (event_callback) {
                event_callback(USB_HOST_EVENT_DEVICE_DISCONNECTED, event_callback_arg);
            }
            break;
            
        default:
            break;
    }
}

bool usb_host_init(usb_host_event_cb_t event_cb, void *arg) {
    esp_err_t ret;
    
#ifdef USB_HOST_DISABLE
    ESP_LOGI(TAG, "USB host initialization disabled (USB_HOST_DISABLE defined)");
    return false;
#endif
    
    event_callback = event_cb;
    event_callback_arg = arg;
    
    ESP_LOGI(TAG, "Initializing USB host mode");
    ESP_LOGI(TAG, "USB pins: D+=%d, D-=%d", USB_DP_PIN, USB_DM_PIN);
    
    // USB host configuration
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Create USB host library task
    xTaskCreate(usb_host_lib_task, "usb_host_lib", 4096, NULL, 5, &usb_host_task_handle);
    
    // Register client for device events
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_device_event_cb,
            .callback_arg = NULL
        }
    };
    
    ret = usb_host_client_register(&client_config, &client_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB host client: %s", esp_err_to_name(ret));
        usb_host_uninstall();
        return false;
    }
    
    // Register MSC client
    const usb_host_client_config_t msc_client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = NULL, // MSC client handles its own events
            .callback_arg = NULL
        }
    };
    
    ret = usb_host_client_register(&msc_client_config, &msc_client_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register MSC client: %s", esp_err_to_name(ret));
        // Continue anyway, we can still detect devices
    }
    
    ESP_LOGI(TAG, "USB host initialized");
    ESP_LOGI(TAG, "Waiting for USB device...");
    
    return true;
}

void usb_host_deinit(void) {
    if (usb_mounted) {
        esp_vfs_fat_unregister_path(USB_MOUNT_POINT);
        usb_mounted = false;
    }
    
    if (msc_client_handle) {
        usb_host_client_deregister(msc_client_handle);
        msc_client_handle = NULL;
    }
    
    if (client_handle) {
        usb_host_client_deregister(client_handle);
        client_handle = NULL;
    }
    
    if (usb_host_task_handle) {
        vTaskDelete(usb_host_task_handle);
        usb_host_task_handle = NULL;
    }
    
    usb_host_uninstall();
    ESP_LOGI(TAG, "USB host deinitialized");
}

bool usb_host_is_mounted(void) {
    return usb_mounted;
}

const char* usb_host_get_mount_point(void) {
    return usb_mounted ? USB_MOUNT_POINT : NULL;
}
