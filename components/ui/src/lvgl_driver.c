/**
 * @file lvgl_driver.c
 * @brief LVGL display and input driver implementation
 */

#include "lvgl_driver.h"
#include "display.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "lvgl_driver";

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_indev_t *indev_touch;
static lv_color_t *disp_draw_buf1 = NULL;
static lv_color_t *disp_draw_buf2 = NULL;
static uint32_t screen_width = 0;
static uint32_t screen_height = 0;
static bool initialized = false;

// Touch state
static lv_indev_data_t touch_data = {0};

/**
 * @brief Display flush callback
 */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Convert LVGL colors to RGB565 and write to display
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            lv_color_t color = color_p[y * w + x];
            uint16_t rgb565 = ((color.ch.red << 8) & 0xF800) | 
                             ((color.ch.green << 3) & 0x07E0) | 
                             ((color.ch.blue >> 3) & 0x001F);
            display_draw_pixel(area->x1 + x, area->y1 + y, rgb565);
        }
    }
    
    display_update();
    lv_disp_flush_ready(disp_drv);
}

/**
 * @brief Touch read callback
 */
static void touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    *data = touch_data;
}

int lvgl_driver_init(uint32_t width, uint32_t height) {
    if (initialized) {
        ESP_LOGW(TAG, "LVGL driver already initialized");
        return 0;
    }
    
    screen_width = width;
    screen_height = height;
    
    ESP_LOGI(TAG, "Initializing LVGL driver: %ux%u", width, height);
    
    // Initialize LVGL
    lv_init();
    
    // Allocate display buffers in PSRAM
    size_t buf_size = width * 40; // 40 lines buffer
    
    disp_draw_buf1 = (lv_color_t*)heap_caps_malloc(
        buf_size * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!disp_draw_buf1) {
        ESP_LOGE(TAG, "Failed to allocate display buffer 1");
        return -1;
    }
    
    disp_draw_buf2 = (lv_color_t*)heap_caps_malloc(
        buf_size * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!disp_draw_buf2) {
        ESP_LOGE(TAG, "Failed to allocate display buffer 2");
        free(disp_draw_buf1);
        return -1;
    }
    
    // Initialize display buffer
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, buf_size);
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // Initialize input device (touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    indev_touch = lv_indev_drv_register(&indev_drv);
    
    initialized = true;
    ESP_LOGI(TAG, "LVGL driver initialized successfully");
    
    return 0;
}

void lvgl_driver_deinit(void) {
    if (!initialized) return;
    
    if (disp_draw_buf1) {
        free(disp_draw_buf1);
        disp_draw_buf1 = NULL;
    }
    
    if (disp_draw_buf2) {
        free(disp_draw_buf2);
        disp_draw_buf2 = NULL;
    }
    
    initialized = false;
    ESP_LOGI(TAG, "LVGL driver deinitialized");
}

void lvgl_driver_process(void) {
    if (!initialized) return;
    lv_timer_handler();
}

void lvgl_driver_handle_touch(uint16_t x, uint16_t y, bool pressed) {
    if (!initialized) return;
    
    touch_data.point.x = x;
    touch_data.point.y = y;
    touch_data.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

