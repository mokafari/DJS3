/**
 * @file lvgl_driver.c
 * @brief LVGL display and input driver implementation
 */

#include "lvgl_driver.h"
#include "gt911.h"
#include "display.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lv_conf_internal.h" // For LV_COLOR_DEPTH
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

// Tick task handle
static TaskHandle_t tick_task_handle = NULL;

/**
 * @brief LVGL tick task - calls lv_tick_inc() periodically
 */
static void lvgl_tick_task(void *pvParameters) {
    const TickType_t delay = pdMS_TO_TICKS(1); // 1ms tick
    
    while (1) {
        lv_tick_inc(1); // Tell LVGL 1ms elapsed
        vTaskDelay(delay);
    }
}

/**
 * @brief Display flush callback
 * Optimized to batch write pixels using single SPI transaction
 */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t pixel_count = w * h;
    
    // Set display window for the entire area
    display_set_window(area->x1, area->y1, area->x2, area->y2);
    
    // Allocate temporary buffer for RGB565 conversion (in internal RAM for speed)
    // Use stack buffer for small areas, heap for large areas
    uint16_t rgb565_buf[256]; // Stack buffer for up to 256 pixels
    uint16_t *rgb565_ptr = rgb565_buf;
    bool use_heap = false;
    
    if (pixel_count > 256) {
        // For large areas, allocate on heap (but try internal RAM first)
        rgb565_ptr = (uint16_t*)heap_caps_malloc(
            pixel_count * sizeof(uint16_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
        if (!rgb565_ptr) {
            // Fallback to any RAM
            rgb565_ptr = (uint16_t*)heap_caps_malloc(
                pixel_count * sizeof(uint16_t),
                MALLOC_CAP_8BIT
            );
        }
        if (!rgb565_ptr) {
            // If allocation fails, use stack buffer in chunks
            rgb565_ptr = rgb565_buf;
            pixel_count = 256; // Limit to stack buffer size
        } else {
            use_heap = true;
        }
    }
    
    // Convert LVGL colors to RGB565
    // If LV_COLOR_DEPTH == 16, lv_color_t is already RGB565 format in the 'full' field
    // Note: display_send_data_batch() handles byte-swapping via MSB_32_16_16_SET macro
    for (uint32_t i = 0; i < pixel_count; i++) {
        lv_color_t color = color_p[i];
#if LV_COLOR_DEPTH == 16
        // LVGL provides RGB565 format - use it directly
        rgb565_ptr[i] = color.full;
#else
        // Manual conversion for other color depths
        rgb565_ptr[i] = ((color.ch.red << 8) & 0xF800) | 
                       ((color.ch.green << 3) & 0x07E0) | 
                       ((color.ch.blue >> 3) & 0x001F);
#endif
    }
    
    // Send entire buffer in one SPI transaction (much faster)
    display_send_data_batch(rgb565_ptr, pixel_count);
    
    // Free heap buffer if used
    if (use_heap && rgb565_ptr != rgb565_buf) {
        free(rgb565_ptr);
    }
    
    lv_disp_flush_ready(disp_drv);
}

/**
 * @brief Touch read callback
 */
static void touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    uint16_t x, y;
    bool pressed;
    // Read from GT911 driver
    if (gt911_read(&x, &y, &pressed) == ESP_OK) {
        // Update static state
        touch_data.point.x = x;
        touch_data.point.y = y;
        touch_data.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    // Return last known state
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
    ESP_LOGI(TAG, "LVGL initialized");
    
    // Allocate display buffer (40 lines, matching Arduino example)
    // Try INTERNAL RAM first (faster, more reliable), then fall back to PSRAM
    size_t buf_size = width * 40; // 40 lines buffer
    size_t buf_bytes = buf_size * sizeof(lv_color_t);
    
    ESP_LOGI(TAG, "Allocating display buffer: %zu bytes (%zu pixels)", buf_bytes, buf_size);
    
    // Try INTERNAL RAM first (like Arduino example)
    disp_draw_buf1 = (lv_color_t*)heap_caps_malloc(
        buf_bytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    
    if (!disp_draw_buf1) {
        ESP_LOGW(TAG, "Internal RAM allocation failed, trying PSRAM...");
        // Fall back to any RAM (includes PSRAM)
        disp_draw_buf1 = (lv_color_t*)heap_caps_malloc(
            buf_bytes,
            MALLOC_CAP_8BIT
        );
    }
    
    if (!disp_draw_buf1) {
        ESP_LOGE(TAG, "Failed to allocate display buffer 1");
        return -1;
    }
    
    ESP_LOGI(TAG, "Display buffer 1 allocated at %p", disp_draw_buf1);
    
    // Small delay between allocations to avoid watchdog issues
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Allocate second buffer (for double buffering)
    disp_draw_buf2 = (lv_color_t*)heap_caps_malloc(
        buf_bytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    
    if (!disp_draw_buf2) {
        ESP_LOGW(TAG, "Internal RAM allocation for buffer 2 failed, trying PSRAM...");
        disp_draw_buf2 = (lv_color_t*)heap_caps_malloc(
            buf_bytes,
            MALLOC_CAP_8BIT
        );
    }
    
    if (!disp_draw_buf2) {
        ESP_LOGW(TAG, "Failed to allocate display buffer 2, using single buffer mode");
        // Single buffer mode (like Arduino example)
        disp_draw_buf2 = NULL;
    } else {
        ESP_LOGI(TAG, "Display buffer 2 allocated at %p", disp_draw_buf2);
    }
    
    // Initialize display buffer
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, buf_size);
    ESP_LOGI(TAG, "Display draw buffer initialized");
    
    // Initialize touch driver
    if (gt911_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GT911 touch driver");
    } else {
        ESP_LOGI(TAG, "GT911 touch driver initialized");
    }
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // Set screen background color (important - otherwise screen appears blank)
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_invalidate(scr); // Force initial redraw
    
    ESP_LOGI(TAG, "Screen background set and invalidated");
    
    // Initialize input device (touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    indev_touch = lv_indev_drv_register(&indev_drv);
    
    // Create tick task for LVGL (calls lv_tick_inc() every 1ms)
    // This is required for LVGL animations and timers to work
    xTaskCreatePinnedToCore(
        lvgl_tick_task,
        "lvgl_tick",
        2048,  // Stack size
        NULL,
        1,  // Priority (low, just needs to run periodically)
        &tick_task_handle,
        1   // Core 1 (to balance load)
    );
    
    if (tick_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL tick task");
        return -1;
    }
    ESP_LOGI(TAG, "LVGL tick task created");
    
    initialized = true;
    ESP_LOGI(TAG, "LVGL driver initialized successfully");
    
    return 0;
}

void lvgl_driver_deinit(void) {
    if (!initialized) return;
    
    // Delete tick task
    if (tick_task_handle != NULL) {
        vTaskDelete(tick_task_handle);
        tick_task_handle = NULL;
    }
    
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