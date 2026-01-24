/**
 * @file storage.h
 * @brief Unified storage abstraction layer supporting USB and SD card
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Storage source types
 */
typedef enum {
    STORAGE_SOURCE_NONE = 0,
    STORAGE_SOURCE_SD_CARD,
    STORAGE_SOURCE_USB
} storage_source_t;

/**
 * @brief Initialize storage abstraction layer
 * 
 * @return true on success, false on failure
 */
bool storage_init(void);

/**
 * @brief Deinitialize storage abstraction layer
 */
void storage_deinit(void);

/**
 * @brief Get current active storage source
 * 
 * @return Storage source type
 */
storage_source_t storage_get_active_source(void);

/**
 * @brief Get mount point path for active storage
 * 
 * @return Mount point path or NULL if no storage available
 */
const char* storage_get_mount_point(void);

/**
 * @brief Check if any storage is available
 * 
 * @return true if storage is available, false otherwise
 */
bool storage_is_available(void);

/**
 * @brief Auto-detect and select best available storage source
 * 
 * Priority: USB > SD Card
 * 
 * @return true if storage source selected, false if none available
 */
bool storage_auto_select(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */

