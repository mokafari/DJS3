/**
 * @file metadata_view.h
 * @brief Metadata display (top zone: title, key, time)
 */

#ifndef METADATA_VIEW_H
#define METADATA_VIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void metadata_view_init(uint32_t width, uint32_t height);
/**
 * @brief Update metadata view
 * 
 * @param title Track title
 * @param key Key
 * @param position Current position
 * @param duration Total duration
 */
void metadata_view_update(const char *title, const char *key, uint32_t position, uint32_t duration);

#ifdef __cplusplus
}
#endif

#endif // METADATA_VIEW_H

