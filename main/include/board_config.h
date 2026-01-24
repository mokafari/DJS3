/**
 * @file board_config.h
 * @brief Board pin configuration for JC4827W543 ESP32-S3
 * 
 * This file contains all GPIO pin definitions for the JC4827W543 board:
 * - Display (NV3041A QSPI)
 * - Touch controller (XPT2046 or GT911)
 * - Backlight control
 * - Other peripherals
 * 
 * PIN CONFLICT CHECK (as of latest update):
 * - GPIO 4: BUTTON_PLAY_PAUSE (OK - GT911 SCL only if capacitive touch used)
 * - GPIO 5: BUTTON_HOT_CUE_1 (moved from GPIO 10 to avoid SD card CS conflict)
 * - GPIO 8: Available (GT911 SDA only if capacitive touch used, we use XPT2046)
 * - GPIO 10: SD_CS_PIN (TF_CS from pinout - SD card Chip Select)
 * - GPIO 11: SD_MISO_PIN (TF_MISO/RTP_DIN - shared with touch controller)
 * - GPIO 12: SD_SCK_PIN (TF_CLK/RTP_CLK - shared with touch controller)
 * - GPIO 13: SD_MOSI_PIN (TF_MOSI/RTP_DIO - shared with touch controller)
 * - GPIO 19: USB_DM (OK - no conflicts)
 * - GPIO 20: USB_DP (OK - no conflicts)
 * - GPIO 35: INPUT-ONLY - Cannot be used for SPI MISO!
 * - GPIO 36: JOG_WHEEL_B (OK - no conflicts)
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

// Uncomment to disable display initialization (useful if display is not connected or causes issues)
// #define DISPLAY_DISABLE

#define SCREEN_WIDTH              480
#define SCREEN_HEIGHT             272
#define SCREEN_BPP                16  // RGB565

/* Display QSPI Pins (4-bit parallel) - Verified from reference */
#define DISPLAY_CS_PIN            45  // Chip Select
#define DISPLAY_SCK_PIN           47  // Serial Clock
#define DISPLAY_D0_PIN            21  // Data bit 0
#define DISPLAY_D1_PIN            48  // Data bit 1
#define DISPLAY_D2_PIN            40  // Data bit 2
#define DISPLAY_D3_PIN            39  // Data bit 3
#define DISPLAY_RST_PIN           (-1) // Not used (GFX_NOT_DEFINED)

/* Display Backlight */
#define DISPLAY_BL_PIN            1   // Backlight PWM (LEDC)

/* ============================================================================
 * Touch Controller Configuration
 * ============================================================================ */

/* Touch Controller Selection */
/* Uncomment one of the following: */
#define TOUCH_XPT2046             1   // Resistive touch (XPT2046)
// #define TOUCH_GT911              1   // Capacitive touch (GT911)

#if defined(TOUCH_XPT2046) && TOUCH_XPT2046

/* XPT2046 Resistive Touch Pins (SPI) - Verified from reference */
#define TOUCH_SCK_PIN             12  // SPI Clock
#define TOUCH_MISO_PIN            13  // SPI MISO
#define TOUCH_MOSI_PIN            11  // SPI MOSI
#define TOUCH_CS_PIN              38  // Chip Select
#define TOUCH_INT_PIN             3   // Interrupt
#define TOUCH_SAMPLES             50  // Number of samples for averaging

/* Touch Calibration (from Arduino_GFX TouchCalibration example) */
#define TOUCH_SWAP_XY             false
#define TOUCH_MAP_X1              230
#define TOUCH_MAP_X2              3860
#define TOUCH_MAP_Y1              3750
#define TOUCH_MAP_Y2              290

#elif defined(TOUCH_GT911) && TOUCH_GT911

/* GT911 Capacitive Touch Pins (I2C) - Verified from reference */
#define TOUCH_SCL_PIN             4   // I2C Clock (CONFLICTS with BUTTON_PLAY_PAUSE if both enabled!)
#define TOUCH_SDA_PIN             8   // I2C Data (CONFLICTS with SD_MOSI if both enabled!)
#define TOUCH_RES_PIN             38  // Reset
#define TOUCH_INT_PIN             3   // Interrupt
#define TOUCH_I2C_ADDR            0x5D  // GT911_SLAVE_ADDRESS1

#endif

/* ============================================================================
 * DJ Controls Configuration
 * ============================================================================ */

/* Button Pins */
#define BUTTON_CUE_PIN            2   // Cue button
#define BUTTON_PLAY_PAUSE_PIN     4   // Play/Pause button (CONFLICTS with GT911 SCL if capacitive touch enabled)
#define BUTTON_SYNC_PIN           6   // Sync button
#define BUTTON_LOOP_IN_PIN        7   // Loop In button
#define BUTTON_LOOP_OUT_PIN       9   // Loop Out button

/* Hot Cue Buttons (4 buttons) */
/* NOTE: GPIO 10 is TF_CS (SD card CS) - moved HOT_CUE_1 to GPIO 5 */
#define BUTTON_HOT_CUE_1_PIN      5   // Moved from GPIO 10 (conflicts with SD card CS)
#define BUTTON_HOT_CUE_2_PIN      14
#define BUTTON_HOT_CUE_3_PIN      15
#define BUTTON_HOT_CUE_4_PIN      16

/* Jog Wheel */
#define JOG_WHEEL_A_PIN           17  // Rotary encoder A
#define JOG_WHEEL_B_PIN           36  // Rotary encoder B (changed from 20 to avoid USB conflict)
#define JOG_WHEEL_TOUCH_PIN       46  // Touch detection for scratch mode

/* Pitch Control */
#define PITCH_ENCODER_A_PIN       33  // Pitch encoder A
#define PITCH_ENCODER_B_PIN       34  // Pitch encoder B

/* ============================================================================
 * Other Peripherals
 * ============================================================================ */

/* ============================================================================
 * Audio Configuration
 * ============================================================================ */

// Uncomment to disable audio output initialization (useful if audio chip is not responding)
#define AUDIO_OUTPUT_DISABLE

/* I2S Audio (Onboard audio chip - NS4168) */
/* Pinout from JC4827W543 board documentation: */
/*   IO2:  SPECK_LRCLK (I2S LRCK/WS) */
/*   IO41: SPECK_DIN (I2S DIN) */
/*   IO42: SPECK_BCLK (I2S BCLK) */
#define I2S_BCLK_PIN              42  // Bit Clock (BCK) - SPECK_BCLK
#define I2S_LRCK_PIN              2   // Word Select (LRCK/WS) - SPECK_LRCLK
#define I2S_DIN_PIN               41  // Data Input (DIN) - SPECK_DIN
#define I2S_MCLK_PIN              (-1) // Master Clock (not used for onboard chip)

/* ============================================================================
 * SD Card Configuration
 * ============================================================================ */

// Uncomment to disable SD card initialization (useful if card is not present or has issues)
#define SD_CARD_DISABLE

// Uncomment to enable verbose SD card error diagnostics
// #define SD_CARD_VERBOSE_ERRORS

/* SD Card (if needed) */
/* NOTE: SD card shares SPI3_HOST bus with touch controller (XPT2046) */
/* Both use same MOSI/MISO/SCK pins but different CS pins */
/* Pinout from JC4827W543 board documentation: */
/*   IO10: TF_CS (SD card Chip Select) */
/*   IO11: RTP_DIN/TF_MISO (Touch DIN / SD card MISO) */
/*   IO12: RTP_CLK/TF_CLK (Touch Clock / SD card Clock) */
/*   IO13: RTP_DIO/TF_MOSI (Touch DIO / SD card MOSI) */
/* Note: Display uses SPI2_HOST with different pins (GPIO 21/47/45) */
#define SD_CS_PIN                 10  // Chip Select (TF_CS from pinout)
#define SD_MOSI_PIN               13  // SPI MOSI (TF_MOSI/RTP_DIO from pinout, shared with touch)
#define SD_MISO_PIN               11  // SPI MISO (TF_MISO/RTP_DIN from pinout, shared with touch)
#define SD_SCK_PIN                12  // SPI Clock (TF_CLK/RTP_CLK from pinout, shared with touch)

/* USB OTG Pins (for USB host mode) */
// Uncomment to disable USB host initialization (useful if it interferes with serial communication)
#define USB_HOST_DISABLE

#define USB_DM_PIN                19  // USB D- (Data Minus)
#define USB_DP_PIN                20  // USB D+ (Data Plus)

/* UART (Serial) */
#define UART_TX_PIN               43  // Default UART0 TX
#define UART_RX_PIN               44  // Default UART0 RX
#define UART_BAUD_RATE            115200

/* ============================================================================
 * LEDC Configuration (Backlight PWM)
 * ============================================================================ */

#define LEDC_CHANNEL_BACKLIGHT    0   // LEDC channel for backlight
#define LEDC_TIMER_BIT            12  // 12-bit precision
#define LEDC_BASE_FREQ            5000 // 5kHz base frequency
#define LEDC_MAX_DUTY              4095 // 2^12 - 1
#define LEDC_DEFAULT_BRIGHTNESS    250 // Default brightness (0-255)

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

/* Check if pin is valid (not -1) */
#define PIN_IS_VALID(pin)         ((pin) >= 0)

/* Convert brightness 0-255 to LEDC duty cycle */
#define BRIGHTNESS_TO_DUTY(brightness) \
    ((uint32_t)((brightness) * LEDC_MAX_DUTY / 255))

#ifdef __cplusplus
}
#endif

#endif /* BOARD_CONFIG_H */
