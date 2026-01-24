/**
 * @file hud_theme.h
 * @brief HUD theme definitions
 */

#ifndef HUD_THEME_H
#define HUD_THEME_H

#include <stdint.h>
#include "ui_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for LVGL types
struct _lv_style_t;
typedef struct _lv_style_t lv_style_t;
struct _lv_color_t {
    uint8_t ch_red;
    uint8_t ch_green;
    uint8_t ch_blue;
};
typedef struct _lv_color_t lv_color_t;

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

