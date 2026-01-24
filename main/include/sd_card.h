/**
 * @file sd_card.h
 * @brief SD card interface for track storage
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SD card interface
 * 
 * @return true on success, false on failure
 */
bool sd_card_init(void);

/**
 * @brief Deinitialize SD card interface
 */
void sd_card_deinit(void);

/**
 * @brief Check if SD card is mounted
 * 
 * @return true if mounted, false otherwise
 */
bool sd_card_is_mounted(void);

/**
 * @brief Get mount point path for SD card
 * 
 * @return Mount point path (e.g., "/sdcard") or NULL if not mounted
 */
const char* sd_card_get_mount_point(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */

