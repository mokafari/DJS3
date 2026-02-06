/**
 * @file board_config.h
 * @brief Board pin configuration for JC4827W543 ESP32-S3 aligned with official pinout
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Board Information
 * ============================================================================ */

#define BOARD_NAME                "JC4827W543"
#define BOARD_MCU                 "ESP32-S3-WROOM-1-N4R8"
#define BOARD_CPU_FREQ_MHZ        240
#define BOARD_PSRAM_SIZE_MB       8
#define BOARD_FLASH_SIZE_MB       4

/* ============================================================================
 * Display Configuration (NV3041A)
 * ============================================================================ */

#define SCREEN_WIDTH              480
#define SCREEN_HEIGHT             272
#define SCREEN_BPP                16  // RGB565

/* Display QSPI Pins (4-bit parallel) - Verified from PDF */
#define DISPLAY_CS_PIN            45  // LCD_CS
#define DISPLAY_SCK_PIN           47  // LCD_CLK
#define DISPLAY_D0_PIN            21  // LCD_D0
#define DISPLAY_D1_PIN            48  // LCD_D1
#define DISPLAY_D2_PIN            40  // LCD_D2
#define DISPLAY_D3_PIN            39  // LCD_D3
#define DISPLAY_RST_PIN           (-1) // Not used
#define DISPLAY_TE_PIN            0    // LCD_TE

/* Display Backlight */
#define DISPLAY_BL_PIN            1   // LCD_BL (LEDC PWM)

/* ============================================================================
 * Touch Controller Configuration
 * ============================================================================ */

/* The board uses GT911 (Capacitive) according to PDF pinout (SCL/SDA) */
#define TOUCH_GT911               1

#if defined(TOUCH_GT911) && TOUCH_GT911
#define TOUCH_SCL_PIN             4   // TOUCH_SCL
#define TOUCH_SDA_PIN             8   // TOUCH_SDA
#define TOUCH_RES_PIN             38  // TOUCH_RES
#define TOUCH_INT_PIN             3   // TOUCH_INT
#define TOUCH_I2C_ADDR            0x5D
#endif

/* ============================================================================
 * Audio Configuration (I2S)
 * ============================================================================ */

/* I2S Audio (Onboard NS4168) - Verified from PDF */
#define I2S_BCLK_PIN              42  // SPECK_BCLK
#define I2S_LRCK_PIN              2   // SPECK_LRCLK
#define I2S_DIN_PIN               41  // SPECK_DIN
#define I2S_MCLK_PIN              (-1)

/* ============================================================================
 * SD Card Configuration
 * ============================================================================ */

// Enabled for track loading
// #define SD_CARD_DISABLE

/* SD Card SPI Pins - Verified from PDF and GitHub discussion */
#define SD_CS_PIN                 10  // TF_CS
#define SD_MOSI_PIN               11  // TF_MOSI (Matched with GitHub: 11)
#define SD_MISO_PIN               13  // TF_MISO (Matched with GitHub: 13)
#define SD_SCK_PIN                12  // TF_CLK

/* ============================================================================
 * DJ Controls Configuration (DISABLED for screen-only setup)
 * ============================================================================ */

/* Hardware controls disabled to avoid pin conflicts and log spam */
#define BUTTON_CUE_PIN            (-1)
#define BUTTON_PLAY_PAUSE_PIN     (-1)
#define BUTTON_SYNC_PIN           (-1)
#define BUTTON_LOOP_IN_PIN        (-1)
#define BUTTON_LOOP_OUT_PIN       (-1)
#define BUTTON_HOT_CUE_1_PIN      (-1)
#define BUTTON_HOT_CUE_2_PIN      (-1)
#define BUTTON_HOT_CUE_3_PIN      (-1)
#define BUTTON_HOT_CUE_4_PIN      (-1)
#define BUTTON_HOT_CUE_5_PIN      (-1)
#define BUTTON_HOT_CUE_6_PIN      (-1)
#define BUTTON_HOT_CUE_7_PIN      (-1)
#define BUTTON_HOT_CUE_8_PIN      (-1)

#define JOG_WHEEL_A_PIN           (-1)
#define JOG_WHEEL_B_PIN           (-1)
#define JOG_WHEEL_TOUCH_PIN       (-1)

#define PITCH_ENCODER_A_PIN       (-1)
#define PITCH_ENCODER_B_PIN       (-1)

/* Pitch Fader ADC (analog slider) */
/* Define the ADC channel for the pitch fader. Set to -1 to disable. */
/* ESP32-S3 ADC1 channels: GPIO1-10 (channels 0-9) */
#define PITCH_FADER_ADC_CHANNEL   (-1)  // Disabled by default
#define PITCH_FADER_ADC_GPIO      (-1)  // Corresponding GPIO (for reference)

/* ============================================================================
 * Other Peripherals
 * ============================================================================ */

/* USB OTG Pins (for USB host mode) */
// Uncomment to disable USB host initialization (useful if it interferes with serial communication)
#define USB_HOST_DISABLE

#define USB_DM_PIN                20  // USB-
#define USB_DP_PIN                19  // USB+
#define UART_TX_PIN               43  // U0TXD
#define UART_RX_PIN               44  // U0RXD

/* ============================================================================
 * LEDC Configuration (Backlight PWM)
 * ============================================================================ */

#define LEDC_CHANNEL_BACKLIGHT    0
#define LEDC_TIMER_BIT            12
#define LEDC_BASE_FREQ            5000
#define LEDC_MAX_DUTY              4095
#define LEDC_DEFAULT_BRIGHTNESS    200

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

#define PIN_IS_VALID(pin)         ((pin) >= 0)
#define BRIGHTNESS_TO_DUTY(brightness) ((uint32_t)((brightness) * LEDC_MAX_DUTY / 255))

#ifdef __cplusplus
}
#endif

#endif /* BOARD_CONFIG_H */