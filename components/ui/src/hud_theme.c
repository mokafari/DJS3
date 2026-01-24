/**
 * @file hud_theme.c
 * @brief High-Contrast HUD theme implementation
 */

#include "hud_theme.h"
#include "lvgl_driver.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "hud_theme";

// Color palettes for each theme (RGB888 format)
static const uint32_t theme_colors[][2] = {
    {0xFFB000, 0x000000},  // Amber on black
    {0x00FFFF, 0x000000},  // Cyan on black
    {0x00FF33, 0x000000}   // Green on black
};

static ui_theme_t current_theme = UI_THEME_AMBER;
static lv_style_t style_phosphor;
static lv_style_t style_bg;
static bool styles_initialized = false;

/**
 * @brief Convert RGB888 to LVGL color
 */
static lv_color_t rgb888_to_lv_color(uint32_t rgb888) {
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >> 8) & 0xFF;
    uint8_t b = rgb888 & 0xFF;
    
    // Convert to LVGL color format (RGB565)
    return lv_color_make(r, g, b);
}

/**
 * @brief Initialize styles
 */
static void init_styles(void) {
    if (styles_initialized) return;
    
    // Background style (always black)
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_black());
    lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);
    lv_style_set_border_width(&style_bg, 0);
    lv_style_set_radius(&style_bg, 0);
    lv_style_set_pad_all(&style_bg, 0);
    
    // Phosphor style (foreground color)
    lv_style_init(&style_phosphor);
    lv_style_set_text_color(&style_phosphor, rgb888_to_lv_color(theme_colors[current_theme][0]));
    lv_style_set_bg_color(&style_phosphor, lv_color_black());
    lv_style_set_border_color(&style_phosphor, rgb888_to_lv_color(theme_colors[current_theme][0]));
    lv_style_set_border_width(&style_phosphor, 2);
    lv_style_set_radius(&style_phosphor, 0); // No rounded corners!
    lv_style_set_text_font(&style_phosphor, &lv_font_montserrat_14); // Monospaced font
    
    styles_initialized = true;
}

void hud_theme_init(ui_theme_t theme) {
    if (theme >= 3) {
        theme = UI_THEME_AMBER; // Default
    }
    
    current_theme = theme;
    ESP_LOGI(TAG, "Initializing HUD theme %d", theme);
    
    init_styles();
}

void hud_theme_apply(ui_theme_t theme) {
    if (theme >= 3) return;
    
    current_theme = theme;
    ESP_LOGI(TAG, "Applying theme %d (color: 0x%06X)", theme, theme_colors[theme][0]);
    
    // Update phosphor style with new color
    if (styles_initialized) {
        lv_color_t fg_color = rgb888_to_lv_color(theme_colors[theme][0]);
        lv_style_set_text_color(&style_phosphor, fg_color);
        lv_style_set_border_color(&style_phosphor, fg_color);
    }
}

uint32_t hud_theme_get_foreground(ui_theme_t theme) {
    if (theme >= 3) return 0xFFB000; // Default to amber
    return theme_colors[theme][0];
}

uint32_t hud_theme_get_background(ui_theme_t theme) {
    (void)theme; // Unused
    return 0x000000; // Always black
}

/**
 * @brief Get LVGL style for phosphor elements
 */
lv_style_t* hud_theme_get_phosphor_style(void) {
    if (!styles_initialized) {
        init_styles();
    }
    return &style_phosphor;
}

/**
 * @brief Get LVGL style for background
 */
lv_style_t* hud_theme_get_bg_style(void) {
    if (!styles_initialized) {
        init_styles();
    }
    return &style_bg;
}

/**
 * @brief Get current theme's foreground color as LVGL color
 */
lv_color_t hud_theme_get_foreground_color(void) {
    return rgb888_to_lv_color(theme_colors[current_theme][0]);
}
