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
void metadata_view_update(const char *title, const char *key, int32_t time_remaining);

#ifdef __cplusplus
}
#endif

#endif // METADATA_VIEW_H

