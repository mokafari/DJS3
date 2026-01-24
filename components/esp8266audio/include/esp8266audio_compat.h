/**
 * @file esp8266audio_compat.h
 * @brief Compatibility layer for ESP8266Audio library on ESP-IDF
 * 
 * Replaces Arduino.h with ESP-IDF equivalents
 */

#ifndef ESP8266AUDIO_COMPAT_H
#define ESP8266AUDIO_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Arduino compatibility macros
#define PROGMEM
#define PSTR(x) x
#define F(x) x
#define pgm_read_byte(x) (*(x))
#define pgm_read_word(x) (*(x))
#define pgm_read_dword(x) (*(x))
#define strcpy_P(dest, src) strcpy(dest, src)
#define strncpy_P(dest, src, n) strncpy(dest, src, n)
#define strlen_P(s) strlen(s)
#define snprintf_P snprintf
#define printf_P printf
#define sprintf_P sprintf
#define memcpy_P(dest, src, n) memcpy(dest, src, n)
#define memcmp_P(s1, s2, n) memcmp(s1, s2, n)

// Delay functions
#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define delayMicroseconds(us) ets_delay_us(us)

// Math functions
#define abs(x) ((x) < 0 ? -(x) : (x))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// Bit manipulation
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

// Type definitions
typedef uint8_t byte;
typedef bool boolean;

// C++ only code
#ifdef __cplusplus
// Print class compatibility (simplified - just use ESP_LOG)
class Print {
public:
    virtual size_t write(uint8_t) { return 0; }
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        for (size_t i = 0; i < size; i++) {
            n += write(buffer[i]);
        }
        return n;
    }
    virtual size_t print(const char *str) {
        ESP_LOGI("Audio", "%s", str);
        return strlen(str);
    }
    virtual size_t println(const char *str) {
        ESP_LOGI("Audio", "%s", str);
        return strlen(str) + 1;
    }
    virtual size_t println() {
        ESP_LOGI("Audio", "");
        return 1;
    }
};

// ESP-IDF log function for audioLogger
extern Print* audioLogger;
#endif // __cplusplus

#endif // ESP8266AUDIO_COMPAT_H

