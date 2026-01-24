/**
 * @file lvgl_driver.h
 * @brief LVGL display and input driver integration
 */

#ifndef LVGL_DRIVER_H
#define LVGL_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL display driver
 * 
 * @param width Screen width
 * @param height Screen height
 * @return 0 on success, negative on error
 */
int lvgl_driver_init(uint32_t width, uint32_t height);

/**
 * @brief Deinitialize LVGL driver
 */
void lvgl_driver_deinit(void);

/**
 * @brief Process LVGL tasks (call in main loop)
 */
void lvgl_driver_process(void);

/**
 * @brief Handle touch input
 * 
 * @param x X coordinate (0-width)
 * @param y Y coordinate (0-height)
 * @param pressed True if pressed
 */
void lvgl_driver_handle_touch(uint16_t x, uint16_t y, bool pressed);

#ifdef __cplusplus
}
#endif

#endif // LVGL_DRIVER_H

