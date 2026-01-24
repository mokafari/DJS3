/**
 * @file display.h
 * @brief NV3041A display driver interface
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 272

/**
 * @brief Initialize NV3041A display
 * 
 * @return true on success, false on failure
 */
bool display_init(void);

/**
 * @brief Check if display is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool display_is_initialized(void);

/**
 * @brief Update display (call periodically)
 */
void display_update(void);

/**
 * @brief Clear display with color
 * 
 * @param color RGB565 color value
 */
void display_clear(uint16_t color);

/**
 * @brief Draw pixel at coordinates
 * 
 * @param x X coordinate (0-479)
 * @param y Y coordinate (0-271)
 * @param color RGB565 color value
 */
void display_draw_pixel(int x, int y, uint16_t color);

/**
 * @brief Draw line
 * 
 * @param x0 Start X
 * @param y0 Start Y
 * @param x1 End X
 * @param y1 End Y
 * @param color RGB565 color value
 */
void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

/**
 * @brief Draw rectangle
 * 
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 * @param color RGB565 color value
 */
void display_draw_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Fill rectangle
 * 
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 * @param color RGB565 color value
 */
void display_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Update track information on display
 * 
 * @param title Track title (or filename)
 * @param artist Artist name (or NULL)
 * @param position Current position in seconds
 * @param duration Total duration in seconds
 */
void display_update_track_info(const char *title, const char *artist, 
                                uint32_t position, uint32_t duration);

/**
 * @brief Set backlight brightness
 * 
 * @param brightness Brightness level (0-255)
 */
void display_set_brightness(uint8_t brightness);

/**
 * @brief Set display window (column and row addresses)
 * 
 * @param x0 Start X
 * @param y0 Start Y
 * @param x1 End X
 * @param y1 End Y
 */
void display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief Send 16-bit data to display
 * 
 * @param data 16-bit data value
 */
void display_send_data16(uint16_t data);

/**
 * @brief Send batch of 16-bit data (optimized for LVGL flush)
 * 
 * @param data Array of 16-bit RGB565 pixel data (const - data is not modified)
 * @param count Number of pixels to send
 * @note Large transfers are automatically split into chunks to respect SPI hardware limits
 */
void display_send_data_batch(const uint16_t *data, size_t count);

/**
 * @brief Enable/disable SPI transaction logging
 * 
 * @param enable true to enable logging, false to disable
 */
void display_enable_spi_logging(bool enable);

/**
 * @brief Fill entire screen with a single color
 * 
 * @param color RGB565 color value
 */
void display_fill_screen(uint16_t color);

/**
 * @brief Test pattern: Fill screen with single color
 * 
 * @param color RGB565 color value (e.g., 0xF800 for RED, 0x07E0 for GREEN, 0x001F for BLUE)
 */
void display_test_single_color(uint16_t color);

/**
 * @brief Test pattern: Draw RGB color bars (Red, Green, Blue, White, Black)
 */
void display_test_color_bars(void);

/**
 * @brief Test pattern: Draw checkerboard pattern
 */
void display_test_checkerboard(void);

/**
 * @brief Test pattern: Draw horizontal gradient
 */
void display_test_gradient(void);

/**
 * @brief Byte-order test: Send known pixels and log packed result
 */
void display_test_byte_order(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
