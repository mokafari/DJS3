#include "usb_host.h"
#include "esp_log.h"

static const char *TAG = "usb_host_mock";

bool usb_host_init(usb_host_event_cb_t event_cb, void *arg) {
    ESP_LOGI(TAG, "Mock USB host init");
    return true;
}

void usb_host_deinit(void) {
}

bool usb_host_is_mounted(void) {
    return false;
}

const char* usb_host_get_mount_point(void) {
    return NULL;
}
