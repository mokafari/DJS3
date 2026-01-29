/**
 * @file gt911.h
 * @brief GT911 capacitive touch controller driver
 */

#ifndef GT911_H
#define GT911_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GT911 touch controller
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gt911_init(void);

/**
 * @brief Read touch data from GT911
 * 
 * @param x Pointer to store X coordinate
 * @param y Pointer to store Y coordinate
 * @param pressed Pointer to store touch state (true = pressed)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gt911_read(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif

#endif /* GT911_H */
