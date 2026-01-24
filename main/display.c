/**
 * @file display.c
 * @brief NV3041A display driver implementation
 * 
 * NV3041A is a 480x272 RGB565 display controller with QSPI interface.
 * This implementation uses SPI mode (4-bit parallel) for communication.
 */

#include "display.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "display";
static spi_device_handle_t spi_handle = NULL;
static uint16_t *framebuffer = NULL;
static bool display_initialized = false;

// NV3041A command definitions
#define NV3041A_NOP             0x00
#define NV3041A_SWRESET         0x01
#define NV3041A_SLPOUT          0x11
#define NV3041A_DISPON          0x29
#define NV3041A_CASET           0x2A
#define NV3041A_RASET           0x2B
#define NV3041A_RAMWR           0x2C
#define NV3041A_MADCTL          0x36
#define NV3041A_COLMOD          0x3A

/**
 * @brief Send command to display
 * Note: NV3041A may use embedded command/data protocol
 * For now, we'll use a simplified approach
 */
static void display_send_cmd(uint8_t cmd) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = SPI_TRANS_USE_TXDATA
    };
    t.tx_data[0] = cmd;
    // NV3041A may not need DC pin - command is embedded in protocol
    spi_device_transmit(spi_handle, &t);
}

/**
 * @brief Send data to display
 */
static void display_send_data(uint8_t *data, size_t len) {
    if (len == 0) return;
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };
    spi_device_transmit(spi_handle, &t);
}

/**
 * @brief Send 16-bit data
 */
static void display_send_data16(uint16_t data) {
    uint8_t buf[2] = {(data >> 8) & 0xFF, data & 0xFF};
    display_send_data(buf, 2);
}

/**
 * @brief Set display window (column and row addresses)
 */
static void display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    display_send_cmd(NV3041A_CASET);
    display_send_data16(x0);
    display_send_data16(x1);
    
    display_send_cmd(NV3041A_RASET);
    display_send_data16(y0);
    display_send_data16(y1);
    
    display_send_cmd(NV3041A_RAMWR);
}

/**
 * @brief Initialize SPI interface for display
 */
static esp_err_t display_spi_init(void) {
    esp_err_t ret;
    
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = DISPLAY_D0_PIN,  // Using D0 as MOSI for SPI mode
        .miso_io_num = -1,
        .sclk_io_num = DISPLAY_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, // RGB565 = 2 bytes/pixel
    };
    
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40 * 1000 * 1000, // 40MHz
        .mode = 0,
        .spics_io_num = DISPLAY_CS_PIN,
        .queue_size = 1,
        .flags = 0,
        .pre_cb = NULL
    };
    
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Note: NV3041A uses QSPI (4-bit parallel) interface
    // This implementation uses SPI mode (single data line) for simplicity
    // Full QSPI support can be added later for better performance
    
    return ESP_OK;
}

/**
 * @brief Initialize NV3041A display controller
 */
static esp_err_t display_controller_init(void) {
    // Software reset
    display_send_cmd(NV3041A_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Sleep out
    display_send_cmd(NV3041A_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Memory access control
    display_send_cmd(NV3041A_MADCTL);
    display_send_data((uint8_t[]){0x00}, 1); // Normal orientation
    
    // Pixel format: RGB565
    display_send_cmd(NV3041A_COLMOD);
    display_send_data((uint8_t[]){0x55}, 1); // 16-bit RGB565
    
    // Display on
    display_send_cmd(NV3041A_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    return ESP_OK;
}

bool display_init(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing NV3041A display");
    ESP_LOGI(TAG, "Resolution: %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Initialize SPI
    ret = display_spi_init();
    if (ret != ESP_OK) {
        return false;
    }
    
    // Initialize display controller
    ret = display_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display controller");
        return false;
    }
    
    // Allocate framebuffer in PSRAM
    framebuffer = (uint16_t*)heap_caps_malloc(
        DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM
    );
    
    if (!framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return false;
    }
    
    // Clear framebuffer
    memset(framebuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
    
    display_initialized = true;
    ESP_LOGI(TAG, "Display initialized successfully");
    
    return true;
}

void display_clear(uint16_t color) {
    if (!display_initialized || !framebuffer) return;
    
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        framebuffer[i] = color;
    }
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (!display_initialized || !framebuffer) return;
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;
    
    framebuffer[y * DISPLAY_WIDTH + x] = color;
}

void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    // Bresenham's line algorithm
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    int x = x0, y = y0;
    
    while (1) {
        display_draw_pixel(x, y, color);
        
        if (x == x1 && y == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void display_draw_rect(int x, int y, int w, int h, uint16_t color) {
    display_draw_line(x, y, x + w - 1, y, color);
    display_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    display_draw_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    display_draw_line(x, y + h - 1, x, y, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!display_initialized || !framebuffer) return;
    
    for (int j = y; j < y + h && j < DISPLAY_HEIGHT; j++) {
        for (int i = x; i < x + w && i < DISPLAY_WIDTH; i++) {
            if (i >= 0 && j >= 0) {
                framebuffer[j * DISPLAY_WIDTH + i] = color;
            }
        }
    }
}

void display_update_track_info(const char *title, const char *artist, 
                                uint32_t position, uint32_t duration) {
    // TODO: Implement text rendering
    (void)title;
    (void)artist;
    (void)position;
    (void)duration;
}

void display_set_brightness(uint8_t brightness) {
    // Backlight is controlled via LEDC in main.c
    (void)brightness;
}

/**
 * @brief Flush framebuffer to display
 */
static void display_flush(void) {
    if (!display_initialized || !framebuffer) return;
    
    display_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    
    // Send framebuffer data
    spi_transaction_t t = {
        .length = DISPLAY_WIDTH * DISPLAY_HEIGHT * 16, // 16 bits per pixel
        .tx_buffer = framebuffer
    };
    
    spi_device_transmit(spi_handle, &t);
}

void display_update(void) {
    if (!display_initialized) return;
    
    // Flush framebuffer to display
    display_flush();
}
