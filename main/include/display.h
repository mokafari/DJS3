/**
 * @file display.h
 * @brief NV3041A display driver interface
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
