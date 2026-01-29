/**
 * @file crate_view.h
 * @brief Crate view interface
 */

#ifndef CRATE_VIEW_H
#define CRATE_VIEW_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void crate_view_init(uint32_t width, uint32_t height);
void crate_view_show(void);
void crate_view_hide(void);
void crate_view_set_tracks(const char **tracks, size_t num_tracks);
void crate_view_set_selection(int index);
void crate_view_cleanup(void);
void crate_view_refresh_tracks(void);

#ifdef __cplusplus
}
#endif

#endif // CRATE_VIEW_H

