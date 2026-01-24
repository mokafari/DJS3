/**
 * @file hud_theme.h
 * @brief HUD theme definitions
 */

#ifndef HUD_THEME_H
#define HUD_THEME_H

#include <stdint.h>
#include "ui_manager.h"
#include "lvgl.h"  // Include LVGL header directly instead of forward declarations

#ifdef __cplusplus
extern "C" {
#endif

void hud_theme_init(ui_theme_t theme);
void hud_theme_apply(ui_theme_t theme);
uint32_t hud_theme_get_foreground(ui_theme_t theme);
uint32_t hud_theme_get_background(ui_theme_t theme);

// LVGL style accessors
lv_style_t* hud_theme_get_phosphor_style(void);
lv_style_t* hud_theme_get_bg_style(void);
lv_color_t hud_theme_get_foreground_color(void);

#ifdef __cplusplus
}
#endif

#endif // HUD_THEME_H

