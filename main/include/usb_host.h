/**
 * @file usb_host.h
 * @brief USB OTG host mode for reading USB sticks
 */

#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB host event types
 */
typedef enum {
    USB_HOST_EVENT_DEVICE_CONNECTED,
    USB_HOST_EVENT_DEVICE_DISCONNECTED,
    USB_HOST_EVENT_MOUNTED,
    USB_HOST_EVENT_UNMOUNTED
} usb_host_event_t;

/**
 * @brief USB host event callback
 */
typedef void (*usb_host_event_cb_t)(usb_host_event_t event, void *arg);

/**
 * @brief Initialize USB host mode
 * 
 * @param event_cb Event callback function (can be NULL)
 * @param arg User argument for callback
 * @return true on success, false on failure
 */
bool usb_host_init(usb_host_event_cb_t event_cb, void *arg);

/**
 * @brief Deinitialize USB host mode
 */
void usb_host_deinit(void);

/**
 * @brief Check if USB device is connected and mounted
 * 
 * @return true if mounted, false otherwise
 */
bool usb_host_is_mounted(void);

/**
 * @brief Get mount point path for USB device
 * 
 * @return Mount point path (e.g., "/usb") or NULL if not mounted
 */
const char* usb_host_get_mount_point(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HOST_H */

