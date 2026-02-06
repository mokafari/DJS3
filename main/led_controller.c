/**
 * @file led_controller.c
 * @brief Comprehensive LED control implementation
 * 
 * Features:
 * - Standard indicator LEDs via GPIO
 * - WS2812/NeoPixel strips via RMT peripheral
 * - Beat-synchronized flashing
 * - VU meter arrays
 * - Animation patterns
 * - Color presets and blending
 */

#include "led_controller.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "led_controller";

/* ============================================================================
 * RMT WS2812 Encoder (ESP-IDF 5.x style)
 * ============================================================================ */

// WS2812 timing (in nanoseconds)
#define WS2812_T0H_NS   350
#define WS2812_T0L_NS   900
#define WS2812_T1H_NS   900
#define WS2812_T1L_NS   350
#define WS2812_RESET_US 280

// RMT resolution (10MHz = 100ns per tick)
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000

/**
 * @brief WS2812 encoder structure
 */
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_ws2812_encoder_t;

/* ============================================================================
 * Internal State
 * ============================================================================ */

// Controller state
static bool led_initialized = false;
static bool led_enabled = true;
static uint8_t global_brightness = 255;
static SemaphoreHandle_t led_mutex = NULL;

// Standard LED state
typedef struct {
    int gpio_pin;
    bool state;
    led_blink_mode_t blink_mode;
    uint32_t blink_phase;
} led_state_t;

static led_state_t indicator_leds[LED_ID_COUNT] = {0};

// WS2812 strip state
static rmt_channel_handle_t led_strip_channel = NULL;
static rmt_encoder_handle_t led_strip_encoder = NULL;
static led_color_t *strip_buffer = NULL;
static uint16_t strip_led_count = 0;
static bool strip_dirty = false;

// Animation state
static led_animation_t current_animation = LED_ANIM_NONE;
static uint8_t animation_speed = 128;
static led_color_t animation_primary = LED_COLOR_WHITE;
static led_color_t animation_secondary = LED_COLOR_OFF;
static uint32_t animation_frame = 0;
static bool animation_paused = false;
static uint64_t last_animation_time = 0;

// Zone state
#define LED_MAX_ZONES 8
static led_strip_zone_t zones[LED_MAX_ZONES] = {0};
static bool zones_active[LED_MAX_ZONES] = {0};

// Beat sync state
static float current_bpm = 120.0f;
static bool beat_sync_enabled = false;
static uint64_t beat_start_time = 0;
static float beat_phase = 0.0f;
static led_beat_callback_t beat_callback = NULL;
static void *beat_callback_arg = NULL;
static uint64_t last_beat_flash_time = 0;
static bool beat_flash_active = false;

// VU meter state
static float vu_level_left = 0.0f;
static float vu_level_right = 0.0f;
static float vu_peak_left = 0.0f;
static float vu_peak_right = 0.0f;
static uint64_t vu_peak_hold_left = 0;
static uint64_t vu_peak_hold_right = 0;
static led_vu_config_t vu_config = {
    .green_threshold = 60,
    .yellow_threshold = 80,
    .red_threshold = 95,
    .peak_hold_ms = 500,
    .decay_rate = 20,
    .show_peak = true
};

// VU meter pin arrays (default to disabled)
static const int vu_left_pins[LED_VU_SEGMENTS] = LED_VU_LEFT_PINS;
static const int vu_right_pins[LED_VU_SEGMENTS] = LED_VU_RIGHT_PINS;

// Hot cue colors
static led_color_t hot_cue_colors[8] = {
    LED_COLOR_OFF, LED_COLOR_OFF, LED_COLOR_OFF, LED_COLOR_OFF,
    LED_COLOR_OFF, LED_COLOR_OFF, LED_COLOR_OFF, LED_COLOR_OFF
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static uint64_t get_time_us(void) {
    return esp_timer_get_time();
}

static uint64_t get_time_ms(void) {
    return esp_timer_get_time() / 1000ULL;
}

/**
 * @brief Clamp value to range
 */
static inline uint8_t clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

/**
 * @brief Apply global brightness to color
 */
static led_color_t apply_brightness(led_color_t color) {
    return (led_color_t){
        .r = (uint8_t)((color.r * global_brightness) / 255),
        .g = (uint8_t)((color.g * global_brightness) / 255),
        .b = (uint8_t)((color.b * global_brightness) / 255)
    };
}

/**
 * @brief Get GPIO pin for LED ID
 */
static int get_led_pin(led_id_t led_id) {
    switch (led_id) {
        case LED_ID_PLAY:      return LED_PLAY_PIN;
        case LED_ID_CUE:       return LED_CUE_PIN;
        case LED_ID_SYNC:      return LED_SYNC_PIN;
        case LED_ID_LOOP:      return LED_LOOP_PIN;
        case LED_ID_SLIP:      return LED_SLIP_PIN;
        case LED_ID_MASTER:    return LED_MASTER_PIN;
        default:               return -1;
    }
}

/* ============================================================================
 * WS2812 RMT Encoder Implementation
 * ============================================================================ */

static size_t rmt_encode_ws2812(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                 const void *primary_data, size_t data_size,
                                 rmt_encode_state_t *ret_state) {
    rmt_ws2812_encoder_t *ws2812_encoder = __containerof(encoder, rmt_ws2812_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = ws2812_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = ws2812_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (ws2812_encoder->state) {
        case 0: // Send RGB data
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data,
                                                     data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                ws2812_encoder->state = 1;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;
            }
            // Fall through
        case 1: // Send reset code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel,
                                                    &ws2812_encoder->reset_code,
                                                    sizeof(ws2812_encoder->reset_code),
                                                    &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                ws2812_encoder->state = RMT_ENCODING_RESET;
                state |= RMT_ENCODING_COMPLETE;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;
            }
            break;
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_ws2812_encoder(rmt_encoder_t *encoder) {
    rmt_ws2812_encoder_t *ws2812_encoder = __containerof(encoder, rmt_ws2812_encoder_t, base);
    rmt_del_encoder(ws2812_encoder->bytes_encoder);
    rmt_del_encoder(ws2812_encoder->copy_encoder);
    free(ws2812_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ws2812_encoder_reset(rmt_encoder_t *encoder) {
    rmt_ws2812_encoder_t *ws2812_encoder = __containerof(encoder, rmt_ws2812_encoder_t, base);
    rmt_encoder_reset(ws2812_encoder->bytes_encoder);
    rmt_encoder_reset(ws2812_encoder->copy_encoder);
    ws2812_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t rmt_new_ws2812_encoder(rmt_encoder_handle_t *ret_encoder) {
    rmt_ws2812_encoder_t *ws2812_encoder = calloc(1, sizeof(rmt_ws2812_encoder_t));
    if (!ws2812_encoder) {
        return ESP_ERR_NO_MEM;
    }

    ws2812_encoder->base.encode = rmt_encode_ws2812;
    ws2812_encoder->base.del = rmt_del_ws2812_encoder;
    ws2812_encoder->base.reset = rmt_ws2812_encoder_reset;

    // Calculate timing ticks at 10MHz (100ns per tick)
    uint32_t t0h_ticks = WS2812_T0H_NS / 100;
    uint32_t t0l_ticks = WS2812_T0L_NS / 100;
    uint32_t t1h_ticks = WS2812_T1H_NS / 100;
    uint32_t t1l_ticks = WS2812_T1L_NS / 100;

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = t0h_ticks,
            .level1 = 0,
            .duration1 = t0l_ticks,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = t1h_ticks,
            .level1 = 0,
            .duration1 = t1l_ticks,
        },
        .flags.msb_first = 1,
    };

    esp_err_t ret = rmt_new_bytes_encoder(&bytes_encoder_config, &ws2812_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        free(ws2812_encoder);
        return ret;
    }

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ret = rmt_new_copy_encoder(&copy_encoder_config, &ws2812_encoder->copy_encoder);
    if (ret != ESP_OK) {
        rmt_del_encoder(ws2812_encoder->bytes_encoder);
        free(ws2812_encoder);
        return ret;
    }

    // Reset code (low for >280us)
    uint32_t reset_ticks = WS2812_RESET_US * (RMT_LED_STRIP_RESOLUTION_HZ / 1000000);
    ws2812_encoder->reset_code = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = reset_ticks / 2,
        .level1 = 0,
        .duration1 = reset_ticks / 2,
    };

    *ret_encoder = &ws2812_encoder->base;
    return ESP_OK;
}

/* ============================================================================
 * Animation Implementations
 * ============================================================================ */

/**
 * @brief Update chase animation
 */
static void anim_chase(uint32_t frame, led_color_t primary, led_color_t secondary,
                       uint16_t start, uint16_t count, uint8_t speed, bool reversed) {
    if (count == 0) return;
    
    uint32_t pos = (frame * speed / 64) % (count * 3);
    
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = reversed ? (start + count - 1 - i) : (start + i);
        if (idx >= strip_led_count) continue;
        
        uint32_t dist = (i * 3 + pos) % (count * 3);
        if (dist < count) {
            // Tail fade
            uint8_t brightness = 255 - (dist * 255 / count);
            strip_buffer[idx] = led_color_scale(primary, brightness);
        } else {
            strip_buffer[idx] = secondary;
        }
    }
}

/**
 * @brief Update pulse animation
 */
static void anim_pulse(uint32_t frame, led_color_t primary,
                       uint16_t start, uint16_t count, uint8_t speed) {
    if (count == 0) return;
    
    // Sine wave brightness
    float phase = (float)(frame * speed) / 2048.0f;
    float brightness_f = (sinf(phase * 2.0f * M_PI) + 1.0f) / 2.0f;
    uint8_t brightness = (uint8_t)(brightness_f * 255.0f);
    
    led_color_t color = led_color_scale(primary, brightness);
    for (uint16_t i = start; i < start + count && i < strip_led_count; i++) {
        strip_buffer[i] = color;
    }
}

/**
 * @brief Update rainbow animation
 */
static void anim_rainbow(uint32_t frame, uint16_t start, uint16_t count, uint8_t speed) {
    if (count == 0) return;
    
    uint32_t hue_offset = (frame * speed / 32) % 256;
    
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = start + i;
        if (idx >= strip_led_count) continue;
        
        uint8_t hue = (uint8_t)((i * 256 / count + hue_offset) % 256);
        strip_buffer[idx] = led_color_rainbow(hue);
    }
}

/**
 * @brief Update rainbow chase animation
 */
static void anim_rainbow_chase(uint32_t frame, uint16_t start, uint16_t count, uint8_t speed) {
    if (count == 0) return;
    
    uint32_t offset = (frame * speed / 32) % count;
    uint32_t hue_offset = (frame * speed / 64) % 256;
    
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = start + i;
        if (idx >= strip_led_count) continue;
        
        uint32_t pos = (i + offset) % count;
        uint8_t brightness = (pos < count / 3) ? (255 - pos * 3) : 0;
        uint8_t hue = (uint8_t)((i * 256 / count + hue_offset) % 256);
        
        led_color_t rainbow = led_color_rainbow(hue);
        strip_buffer[idx] = led_color_scale(rainbow, brightness);
    }
}

/**
 * @brief Update sparkle animation
 */
static void anim_sparkle(uint32_t frame, led_color_t primary, led_color_t secondary,
                         uint16_t start, uint16_t count, uint8_t intensity) {
    if (count == 0) return;
    
    // Fade existing pixels
    for (uint16_t i = start; i < start + count && i < strip_led_count; i++) {
        strip_buffer[i].r = strip_buffer[i].r > 10 ? strip_buffer[i].r - 10 : 0;
        strip_buffer[i].g = strip_buffer[i].g > 10 ? strip_buffer[i].g - 10 : 0;
        strip_buffer[i].b = strip_buffer[i].b > 10 ? strip_buffer[i].b - 10 : 0;
    }
    
    // Add random sparkles
    uint32_t num_sparkles = (intensity * count) / 1024 + 1;
    for (uint32_t s = 0; s < num_sparkles; s++) {
        // Simple pseudo-random based on frame
        uint32_t rand_val = (frame * 31337 + s * 7919) ^ (frame >> 3);
        uint16_t idx = start + (rand_val % count);
        if (idx < strip_led_count) {
            strip_buffer[idx] = primary;
        }
    }
}

/**
 * @brief Update breathe animation (slower, smoother pulse)
 */
static void anim_breathe(uint32_t frame, led_color_t primary,
                         uint16_t start, uint16_t count, uint8_t speed) {
    if (count == 0) return;
    
    // Quadratic ease-in-out breathing
    float phase = fmodf((float)(frame * speed) / 8192.0f, 1.0f);
    float brightness_f;
    if (phase < 0.5f) {
        brightness_f = 2.0f * phase * phase;
    } else {
        phase = phase - 0.5f;
        brightness_f = 1.0f - 2.0f * phase * phase;
    }
    uint8_t brightness = (uint8_t)(brightness_f * 255.0f);
    
    led_color_t color = led_color_scale(primary, brightness);
    for (uint16_t i = start; i < start + count && i < strip_led_count; i++) {
        strip_buffer[i] = color;
    }
}

/**
 * @brief Update fire animation
 */
static void anim_fire(uint32_t frame, uint16_t start, uint16_t count, uint8_t intensity) {
    if (count == 0) return;
    
    // Cooling and sparking parameters
    uint8_t cooling = 55;
    uint8_t sparking = intensity;
    
    // Heat array (simplified - stored in buffer red channel temporarily)
    static uint8_t heat[LED_WS2812_MAX_LEDS];
    
    // Cool down
    for (uint16_t i = 0; i < count; i++) {
        uint32_t rand_val = (frame * 31337 + i * 7919) % 256;
        int cooldown = (rand_val * cooling / count) / 10;
        heat[i] = (heat[i] > cooldown) ? heat[i] - cooldown : 0;
    }
    
    // Heat rises
    for (uint16_t i = count - 1; i >= 2; i--) {
        heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3;
    }
    
    // Random sparks at bottom
    uint32_t rand_val = (frame * 31337) % 256;
    if (rand_val < sparking) {
        uint8_t y = (rand_val * 7) % 8;
        heat[y] = heat[y] + 160 + (rand_val % 96);
        if (heat[y] > 255) heat[y] = 255;
    }
    
    // Convert heat to color
    for (uint16_t i = 0; i < count && (start + i) < strip_led_count; i++) {
        uint8_t h = heat[i];
        led_color_t color;
        if (h > 200) {
            color = (led_color_t){255, 255, (h - 200) * 4};
        } else if (h > 100) {
            color = (led_color_t){255, (h - 100) * 2, 0};
        } else {
            color = (led_color_t){h * 2, 0, 0};
        }
        strip_buffer[start + i] = color;
    }
}

/**
 * @brief Update wave animation
 */
static void anim_wave(uint32_t frame, led_color_t primary, led_color_t secondary,
                      uint16_t start, uint16_t count, uint8_t speed) {
    if (count == 0) return;
    
    float phase = (float)(frame * speed) / 1024.0f;
    
    for (uint16_t i = 0; i < count && (start + i) < strip_led_count; i++) {
        float pos = (float)i / (float)count;
        float wave = (sinf((pos * 4.0f + phase) * 2.0f * M_PI) + 1.0f) / 2.0f;
        uint8_t blend = (uint8_t)(wave * 255.0f);
        strip_buffer[start + i] = led_color_blend(secondary, primary, blend);
    }
}

/**
 * @brief Update VU meter animation on strip
 */
static void anim_vu_meter_strip(uint16_t start, uint16_t count) {
    if (count == 0) return;
    
    uint16_t half = count / 2;
    float level_l = vu_level_left;
    float level_r = vu_level_right;
    
    // Left channel (first half, reversed)
    for (uint16_t i = 0; i < half && (start + i) < strip_led_count; i++) {
        float pos = (float)(half - 1 - i) / (float)half;
        led_color_t color;
        
        if (pos * 100 < vu_config.green_threshold) {
            color = LED_COLOR_GREEN;
        } else if (pos * 100 < vu_config.yellow_threshold) {
            color = LED_COLOR_YELLOW;
        } else {
            color = LED_COLOR_RED;
        }
        
        if (pos <= level_l) {
            strip_buffer[start + i] = color;
        } else {
            strip_buffer[start + i] = LED_COLOR_OFF;
        }
    }
    
    // Right channel (second half)
    for (uint16_t i = 0; i < half && (start + half + i) < strip_led_count; i++) {
        float pos = (float)i / (float)half;
        led_color_t color;
        
        if (pos * 100 < vu_config.green_threshold) {
            color = LED_COLOR_GREEN;
        } else if (pos * 100 < vu_config.yellow_threshold) {
            color = LED_COLOR_YELLOW;
        } else {
            color = LED_COLOR_RED;
        }
        
        if (pos <= level_r) {
            strip_buffer[start + half + i] = color;
        } else {
            strip_buffer[start + half + i] = LED_COLOR_OFF;
        }
    }
}

/**
 * @brief Update beat pulse animation
 */
static void anim_beat_pulse(led_color_t primary, uint16_t start, uint16_t count) {
    if (count == 0) return;
    
    uint8_t brightness = 0;
    if (beat_flash_active) {
        uint64_t now = get_time_ms();
        uint64_t elapsed = now - last_beat_flash_time;
        if (elapsed < LED_BEAT_FLASH_DURATION_MS) {
            brightness = 255 - (elapsed * 255 / LED_BEAT_FLASH_DURATION_MS);
        } else {
            beat_flash_active = false;
        }
    }
    
    led_color_t color = led_color_scale(primary, brightness);
    for (uint16_t i = start; i < start + count && i < strip_led_count; i++) {
        strip_buffer[i] = color;
    }
}

/**
 * @brief Update beat strobe animation
 */
static void anim_beat_strobe(led_color_t primary, uint16_t start, uint16_t count) {
    if (count == 0) return;
    
    led_color_t color = beat_flash_active ? primary : LED_COLOR_OFF;
    for (uint16_t i = start; i < start + count && i < strip_led_count; i++) {
        strip_buffer[i] = color;
    }
}

/* ============================================================================
 * Core Functions
 * ============================================================================ */

bool led_controller_init(void) {
    ESP_LOGI(TAG, "Initializing LED controller");
    
    if (led_initialized) {
        ESP_LOGW(TAG, "LED controller already initialized");
        return true;
    }
    
    // Create mutex
    led_mutex = xSemaphoreCreateMutex();
    if (!led_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Initialize indicator LED GPIOs
    for (int i = 0; i < LED_ID_COUNT; i++) {
        int pin = get_led_pin((led_id_t)i);
        indicator_leds[i].gpio_pin = pin;
        indicator_leds[i].state = false;
        indicator_leds[i].blink_mode = LED_BLINK_OFF;
        indicator_leds[i].blink_phase = 0;
        
        if (PIN_IS_VALID(pin)) {
            gpio_config_t io_conf = {
                .pin_bit_mask = (1ULL << pin),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&io_conf);
            gpio_set_level(pin, 0);
        }
    }
    
    // Initialize VU meter GPIOs
    for (int i = 0; i < LED_VU_SEGMENTS; i++) {
        if (PIN_IS_VALID(vu_left_pins[i])) {
            gpio_config_t io_conf = {
                .pin_bit_mask = (1ULL << vu_left_pins[i]),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&io_conf);
            gpio_set_level(vu_left_pins[i], 0);
        }
        if (PIN_IS_VALID(vu_right_pins[i])) {
            gpio_config_t io_conf = {
                .pin_bit_mask = (1ULL << vu_right_pins[i]),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&io_conf);
            gpio_set_level(vu_right_pins[i], 0);
        }
    }
    
    // Initialize WS2812 strip if configured
    if (PIN_IS_VALID(LED_WS2812_PIN) && LED_WS2812_COUNT > 0) {
        strip_led_count = LED_WS2812_COUNT;
        if (strip_led_count > LED_WS2812_MAX_LEDS) {
            strip_led_count = LED_WS2812_MAX_LEDS;
        }
        
        // Allocate buffer
        strip_buffer = calloc(strip_led_count, sizeof(led_color_t));
        if (!strip_buffer) {
            ESP_LOGE(TAG, "Failed to allocate strip buffer");
            vSemaphoreDelete(led_mutex);
            led_mutex = NULL;
            return false;
        }
        
        // Configure RMT channel
        rmt_tx_channel_config_t tx_chan_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .gpio_num = LED_WS2812_PIN,
            .mem_block_symbols = 64,
            .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
            .trans_queue_depth = 4,
        };
        
        esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led_strip_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
            free(strip_buffer);
            strip_buffer = NULL;
            vSemaphoreDelete(led_mutex);
            led_mutex = NULL;
            return false;
        }
        
        // Create encoder
        ret = rmt_new_ws2812_encoder(&led_strip_encoder);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create WS2812 encoder: %s", esp_err_to_name(ret));
            rmt_del_channel(led_strip_channel);
            led_strip_channel = NULL;
            free(strip_buffer);
            strip_buffer = NULL;
            vSemaphoreDelete(led_mutex);
            led_mutex = NULL;
            return false;
        }
        
        // Enable channel
        ret = rmt_enable(led_strip_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
            rmt_del_encoder(led_strip_encoder);
            led_strip_encoder = NULL;
            rmt_del_channel(led_strip_channel);
            led_strip_channel = NULL;
            free(strip_buffer);
            strip_buffer = NULL;
            vSemaphoreDelete(led_mutex);
            led_mutex = NULL;
            return false;
        }
        
        ESP_LOGI(TAG, "WS2812 strip initialized: %d LEDs on GPIO %d", strip_led_count, LED_WS2812_PIN);
    }
    
    // Initialize timing
    beat_start_time = get_time_us();
    last_animation_time = get_time_us();
    
    led_initialized = true;
    led_enabled = true;
    
    ESP_LOGI(TAG, "LED controller initialized");
    return true;
}

void led_controller_deinit(void) {
    if (!led_initialized) return;
    
    ESP_LOGI(TAG, "Deinitializing LED controller");
    
    // Turn off all LEDs
    led_set_all(false);
    led_strip_clear();
    led_strip_refresh();
    
    // Free WS2812 resources
    if (led_strip_channel) {
        rmt_disable(led_strip_channel);
        if (led_strip_encoder) {
            rmt_del_encoder(led_strip_encoder);
            led_strip_encoder = NULL;
        }
        rmt_del_channel(led_strip_channel);
        led_strip_channel = NULL;
    }
    
    if (strip_buffer) {
        free(strip_buffer);
        strip_buffer = NULL;
    }
    
    if (led_mutex) {
        vSemaphoreDelete(led_mutex);
        led_mutex = NULL;
    }
    
    led_initialized = false;
    ESP_LOGI(TAG, "LED controller deinitialized");
}

void led_controller_update(void) {
    if (!led_initialized || !led_enabled) return;
    
    uint64_t now_us = get_time_us();
    uint64_t now_ms = now_us / 1000;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    
    // === Update beat sync ===
    if (beat_sync_enabled && current_bpm > 0) {
        float beat_duration_us = 60000000.0f / current_bpm;
        uint64_t elapsed = now_us - beat_start_time;
        float current_phase = fmodf((float)elapsed / beat_duration_us, 1.0f);
        
        // Check for beat transition
        if (current_phase < beat_phase) {
            // Beat occurred
            led_trigger_beat();
        }
        beat_phase = current_phase;
    }
    
    // === Update indicator LED blink states ===
    for (int i = 0; i < LED_ID_COUNT; i++) {
        if (!PIN_IS_VALID(indicator_leds[i].gpio_pin)) continue;
        
        bool output = false;
        switch (indicator_leds[i].blink_mode) {
            case LED_BLINK_OFF:
                output = false;
                break;
            case LED_BLINK_SOLID:
                output = indicator_leds[i].state;
                break;
            case LED_BLINK_SLOW:
                output = indicator_leds[i].state && ((now_ms / 500) % 2 == 0);
                break;
            case LED_BLINK_FAST:
                output = indicator_leds[i].state && ((now_ms / 125) % 2 == 0);
                break;
            case LED_BLINK_BEAT:
                output = indicator_leds[i].state && beat_flash_active;
                break;
            case LED_BLINK_PULSE: {
                if (indicator_leds[i].state) {
                    float phase = fmodf((float)now_ms / 1000.0f, 1.0f);
                    output = (sinf(phase * 2.0f * M_PI) + 1.0f) / 2.0f > 0.5f;
                } else {
                    output = false;
                }
                break;
            }
        }
        gpio_set_level(indicator_leds[i].gpio_pin, output ? 1 : 0);
    }
    
    // === Update VU meter GPIOs ===
    for (int i = 0; i < LED_VU_SEGMENTS; i++) {
        float threshold = (float)(i + 1) / (float)LED_VU_SEGMENTS;
        
        // Left channel
        if (PIN_IS_VALID(vu_left_pins[i])) {
            bool on = vu_level_left >= threshold;
            // Peak hold
            if (vu_config.show_peak && vu_peak_left >= threshold && 
                now_ms - vu_peak_hold_left < vu_config.peak_hold_ms) {
                on = true;
            }
            gpio_set_level(vu_left_pins[i], on ? 1 : 0);
        }
        
        // Right channel
        if (PIN_IS_VALID(vu_right_pins[i])) {
            bool on = vu_level_right >= threshold;
            if (vu_config.show_peak && vu_peak_right >= threshold && 
                now_ms - vu_peak_hold_right < vu_config.peak_hold_ms) {
                on = true;
            }
            gpio_set_level(vu_right_pins[i], on ? 1 : 0);
        }
    }
    
    // === Update WS2812 animations ===
    if (strip_buffer && strip_led_count > 0) {
        // Calculate frame delta
        uint64_t frame_delta_us = now_us - last_animation_time;
        if (frame_delta_us >= (1000000 / LED_ANIMATION_FPS)) {
            last_animation_time = now_us;
            
            if (!animation_paused) {
                animation_frame++;
                
                // Check for zone-based animations first
                bool has_zones = false;
                for (int z = 0; z < LED_MAX_ZONES; z++) {
                    if (zones_active[z]) {
                        has_zones = true;
                        led_strip_zone_t *zone = &zones[z];
                        
                        switch (zone->animation) {
                            case LED_ANIM_CHASE:
                                anim_chase(animation_frame, zone->primary_color, zone->secondary_color,
                                          zone->start_led, zone->led_count, zone->speed, zone->reversed);
                                break;
                            case LED_ANIM_PULSE:
                                anim_pulse(animation_frame, zone->primary_color,
                                          zone->start_led, zone->led_count, zone->speed);
                                break;
                            case LED_ANIM_RAINBOW:
                                anim_rainbow(animation_frame, zone->start_led, zone->led_count, zone->speed);
                                break;
                            case LED_ANIM_RAINBOW_CHASE:
                                anim_rainbow_chase(animation_frame, zone->start_led, zone->led_count, zone->speed);
                                break;
                            case LED_ANIM_SPARKLE:
                                anim_sparkle(animation_frame, zone->primary_color, zone->secondary_color,
                                            zone->start_led, zone->led_count, zone->intensity);
                                break;
                            case LED_ANIM_BREATHE:
                                anim_breathe(animation_frame, zone->primary_color,
                                            zone->start_led, zone->led_count, zone->speed);
                                break;
                            case LED_ANIM_FIRE:
                                anim_fire(animation_frame, zone->start_led, zone->led_count, zone->intensity);
                                break;
                            case LED_ANIM_VU_METER:
                                anim_vu_meter_strip(zone->start_led, zone->led_count);
                                break;
                            case LED_ANIM_BEAT_PULSE:
                                anim_beat_pulse(zone->primary_color, zone->start_led, zone->led_count);
                                break;
                            case LED_ANIM_BEAT_STROBE:
                                anim_beat_strobe(zone->primary_color, zone->start_led, zone->led_count);
                                break;
                            case LED_ANIM_WAVE:
                                anim_wave(animation_frame, zone->primary_color, zone->secondary_color,
                                         zone->start_led, zone->led_count, zone->speed);
                                break;
                            default:
                                // Static color
                                for (uint16_t i = zone->start_led; 
                                     i < zone->start_led + zone->led_count && i < strip_led_count; i++) {
                                    strip_buffer[i] = zone->primary_color;
                                }
                                break;
                        }
                    }
                }
                
                // Apply global animation if no zones are active
                if (!has_zones && current_animation != LED_ANIM_NONE) {
                    switch (current_animation) {
                        case LED_ANIM_CHASE:
                            anim_chase(animation_frame, animation_primary, animation_secondary,
                                      0, strip_led_count, animation_speed, false);
                            break;
                        case LED_ANIM_PULSE:
                            anim_pulse(animation_frame, animation_primary, 0, strip_led_count, animation_speed);
                            break;
                        case LED_ANIM_RAINBOW:
                            anim_rainbow(animation_frame, 0, strip_led_count, animation_speed);
                            break;
                        case LED_ANIM_RAINBOW_CHASE:
                            anim_rainbow_chase(animation_frame, 0, strip_led_count, animation_speed);
                            break;
                        case LED_ANIM_SPARKLE:
                            anim_sparkle(animation_frame, animation_primary, animation_secondary,
                                        0, strip_led_count, animation_speed);
                            break;
                        case LED_ANIM_BREATHE:
                            anim_breathe(animation_frame, animation_primary, 0, strip_led_count, animation_speed);
                            break;
                        case LED_ANIM_FIRE:
                            anim_fire(animation_frame, 0, strip_led_count, animation_speed);
                            break;
                        case LED_ANIM_VU_METER:
                            anim_vu_meter_strip(0, strip_led_count);
                            break;
                        case LED_ANIM_BEAT_PULSE:
                            anim_beat_pulse(animation_primary, 0, strip_led_count);
                            break;
                        case LED_ANIM_BEAT_STROBE:
                            anim_beat_strobe(animation_primary, 0, strip_led_count);
                            break;
                        case LED_ANIM_WAVE:
                            anim_wave(animation_frame, animation_primary, animation_secondary,
                                     0, strip_led_count, animation_speed);
                            break;
                        default:
                            break;
                    }
                }
                
                strip_dirty = true;
            }
        }
        
        // Refresh strip if dirty
        if (strip_dirty) {
            led_strip_refresh();
            strip_dirty = false;
        }
    }
    
    // === Decay VU levels ===
    float decay = (float)vu_config.decay_rate / 1000.0f;
    if (vu_level_left > 0) {
        vu_level_left -= decay;
        if (vu_level_left < 0) vu_level_left = 0;
    }
    if (vu_level_right > 0) {
        vu_level_right -= decay;
        if (vu_level_right < 0) vu_level_right = 0;
    }
    
    // === Update beat flash state ===
    if (beat_flash_active) {
        if (now_ms - last_beat_flash_time >= LED_BEAT_FLASH_DURATION_MS) {
            beat_flash_active = false;
        }
    }
    
    xSemaphoreGive(led_mutex);
}

void led_controller_set_brightness(uint8_t brightness) {
    global_brightness = brightness;
}

uint8_t led_controller_get_brightness(void) {
    return global_brightness;
}

void led_controller_enable(bool enabled) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    led_enabled = enabled;
    if (!enabled) {
        // Turn off all LEDs immediately
        for (int i = 0; i < LED_ID_COUNT; i++) {
            if (PIN_IS_VALID(indicator_leds[i].gpio_pin)) {
                gpio_set_level(indicator_leds[i].gpio_pin, 0);
            }
        }
        led_strip_clear();
        led_strip_refresh();
    }
    xSemaphoreGive(led_mutex);
}

bool led_controller_is_enabled(void) {
    return led_enabled;
}

/* ============================================================================
 * Standard Indicator LED Functions
 * ============================================================================ */

void led_set(led_id_t led_id, bool on) {
    if (led_id >= LED_ID_COUNT) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    indicator_leds[led_id].state = on;
    if (indicator_leds[led_id].blink_mode == LED_BLINK_OFF) {
        indicator_leds[led_id].blink_mode = LED_BLINK_SOLID;
    }
    xSemaphoreGive(led_mutex);
}

bool led_get(led_id_t led_id) {
    if (led_id >= LED_ID_COUNT) return false;
    return indicator_leds[led_id].state;
}

void led_set_blink_mode(led_id_t led_id, led_blink_mode_t mode) {
    if (led_id >= LED_ID_COUNT) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    indicator_leds[led_id].blink_mode = mode;
    xSemaphoreGive(led_mutex);
}

void led_set_all(bool on) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    for (int i = 0; i < LED_ID_COUNT; i++) {
        indicator_leds[i].state = on;
        if (indicator_leds[i].blink_mode == LED_BLINK_OFF) {
            indicator_leds[i].blink_mode = LED_BLINK_SOLID;
        }
    }
    xSemaphoreGive(led_mutex);
}

void led_toggle(led_id_t led_id) {
    if (led_id >= LED_ID_COUNT) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    indicator_leds[led_id].state = !indicator_leds[led_id].state;
    xSemaphoreGive(led_mutex);
}

/* ============================================================================
 * Beat Synchronization Functions
 * ============================================================================ */

void led_set_bpm(float bpm) {
    if (bpm < 20.0f) bpm = 20.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    current_bpm = bpm;
    xSemaphoreGive(led_mutex);
}

float led_get_bpm(void) {
    return current_bpm;
}

void led_trigger_beat(void) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    beat_flash_active = true;
    last_beat_flash_time = get_time_ms();
    
    // Call callback
    if (beat_callback) {
        beat_callback(current_bpm, beat_phase, beat_callback_arg);
    }
    xSemaphoreGive(led_mutex);
}

void led_set_beat_phase(float phase) {
    if (phase < 0.0f) phase = 0.0f;
    if (phase > 1.0f) phase = 1.0f;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    beat_phase = phase;
    // Recalculate beat start time to align
    float beat_duration_us = 60000000.0f / current_bpm;
    beat_start_time = get_time_us() - (uint64_t)(phase * beat_duration_us);
    xSemaphoreGive(led_mutex);
}

void led_set_beat_sync(bool enabled) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    beat_sync_enabled = enabled;
    if (enabled) {
        beat_start_time = get_time_us();
        beat_phase = 0.0f;
    }
    xSemaphoreGive(led_mutex);
}

void led_set_beat_callback(led_beat_callback_t callback, void *arg) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    beat_callback = callback;
    beat_callback_arg = arg;
    xSemaphoreGive(led_mutex);
}

/* ============================================================================
 * WS2812 Strip Functions
 * ============================================================================ */

void led_strip_set_pixel(uint16_t index, led_color_t color) {
    if (!strip_buffer || index >= strip_led_count) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    strip_buffer[index] = color;
    strip_dirty = true;
    xSemaphoreGive(led_mutex);
}

void led_strip_set_range(uint16_t start, uint16_t count, led_color_t color) {
    if (!strip_buffer) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    for (uint16_t i = start; i < start + count && i < strip_led_count; i++) {
        strip_buffer[i] = color;
    }
    strip_dirty = true;
    xSemaphoreGive(led_mutex);
}

void led_strip_set_all(led_color_t color) {
    if (!strip_buffer) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    for (uint16_t i = 0; i < strip_led_count; i++) {
        strip_buffer[i] = color;
    }
    strip_dirty = true;
    xSemaphoreGive(led_mutex);
}

void led_strip_clear(void) {
    if (!strip_buffer) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    memset(strip_buffer, 0, strip_led_count * sizeof(led_color_t));
    strip_dirty = true;
    xSemaphoreGive(led_mutex);
}

void led_strip_refresh(void) {
    if (!strip_buffer || !led_strip_channel || !led_strip_encoder || strip_led_count == 0) return;
    
    // Create GRB data buffer (WS2812 uses GRB order)
    uint8_t *grb_data = malloc(strip_led_count * 3);
    if (!grb_data) return;
    
    for (uint16_t i = 0; i < strip_led_count; i++) {
        led_color_t color = apply_brightness(strip_buffer[i]);
        grb_data[i * 3 + 0] = color.g;
        grb_data[i * 3 + 1] = color.r;
        grb_data[i * 3 + 2] = color.b;
    }
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    
    rmt_transmit(led_strip_channel, led_strip_encoder, grb_data, strip_led_count * 3, &tx_config);
    rmt_tx_wait_all_done(led_strip_channel, pdMS_TO_TICKS(100));
    
    free(grb_data);
}

uint16_t led_strip_get_count(void) {
    return strip_led_count;
}

/* ============================================================================
 * Animation Functions
 * ============================================================================ */

void led_strip_set_animation(led_animation_t animation) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    current_animation = animation;
    animation_frame = 0;
    xSemaphoreGive(led_mutex);
}

led_animation_t led_strip_get_animation(void) {
    return current_animation;
}

void led_strip_set_animation_speed(uint8_t speed) {
    animation_speed = speed;
}

void led_strip_set_primary_color(led_color_t color) {
    animation_primary = color;
}

void led_strip_set_secondary_color(led_color_t color) {
    animation_secondary = color;
}

void led_strip_pause_animation(void) {
    animation_paused = true;
}

void led_strip_resume_animation(void) {
    animation_paused = false;
}

void led_strip_reset_animation(void) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    animation_frame = 0;
    xSemaphoreGive(led_mutex);
}

/* ============================================================================
 * Zone Functions
 * ============================================================================ */

bool led_strip_set_zone(uint8_t zone_id, const led_strip_zone_t *zone) {
    if (zone_id >= LED_MAX_ZONES || !zone) return false;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    zones[zone_id] = *zone;
    zones_active[zone_id] = true;
    xSemaphoreGive(led_mutex);
    
    return true;
}

bool led_strip_get_zone(uint8_t zone_id, led_strip_zone_t *zone) {
    if (zone_id >= LED_MAX_ZONES || !zone) return false;
    if (!zones_active[zone_id]) return false;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    *zone = zones[zone_id];
    xSemaphoreGive(led_mutex);
    
    return true;
}

void led_strip_clear_zone(uint8_t zone_id) {
    if (zone_id >= LED_MAX_ZONES) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    zones_active[zone_id] = false;
    memset(&zones[zone_id], 0, sizeof(led_strip_zone_t));
    xSemaphoreGive(led_mutex);
}

void led_strip_clear_all_zones(void) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    for (int i = 0; i < LED_MAX_ZONES; i++) {
        zones_active[i] = false;
        memset(&zones[i], 0, sizeof(led_strip_zone_t));
    }
    xSemaphoreGive(led_mutex);
}

/* ============================================================================
 * VU Meter Functions
 * ============================================================================ */

void led_vu_set_left(float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    vu_level_left = level;
    if (level > vu_peak_left) {
        vu_peak_left = level;
        vu_peak_hold_left = get_time_ms();
    }
    xSemaphoreGive(led_mutex);
}

void led_vu_set_right(float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    vu_level_right = level;
    if (level > vu_peak_right) {
        vu_peak_right = level;
        vu_peak_hold_right = get_time_ms();
    }
    xSemaphoreGive(led_mutex);
}

void led_vu_set_levels(float left, float right) {
    led_vu_set_left(left);
    led_vu_set_right(right);
}

void led_vu_configure(const led_vu_config_t *config) {
    if (!config) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    vu_config = *config;
    xSemaphoreGive(led_mutex);
}

void led_vu_set_peak_hold(bool enabled) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    vu_config.show_peak = enabled;
    xSemaphoreGive(led_mutex);
}

/* ============================================================================
 * Button Illumination Functions
 * ============================================================================ */

void led_set_hot_cue_color(uint8_t hot_cue_num, led_color_t color) {
    if (hot_cue_num < 1 || hot_cue_num > 8) return;
    
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    hot_cue_colors[hot_cue_num - 1] = color;
    xSemaphoreGive(led_mutex);
}

void led_set_hot_cue_preset(uint8_t hot_cue_num, led_color_preset_t preset) {
    led_set_hot_cue_color(hot_cue_num, led_color_from_preset(preset));
}

void led_clear_hot_cue_colors(void) {
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    for (int i = 0; i < 8; i++) {
        hot_cue_colors[i] = LED_COLOR_OFF;
    }
    xSemaphoreGive(led_mutex);
}

/* ============================================================================
 * Color Utility Functions
 * ============================================================================ */

led_color_t led_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (led_color_t){r, g, b};
}

led_color_t led_color_hsv(uint16_t h, uint8_t s, uint8_t v) {
    // HSV to RGB conversion
    if (s == 0) {
        return (led_color_t){v, v, v};
    }
    
    h = h % 360;
    uint8_t region = h / 60;
    uint8_t remainder = (h - (region * 60)) * 255 / 60;
    
    uint8_t p = (v * (255 - s)) / 255;
    uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;
    
    switch (region) {
        case 0:  return (led_color_t){v, t, p};
        case 1:  return (led_color_t){q, v, p};
        case 2:  return (led_color_t){p, v, t};
        case 3:  return (led_color_t){p, q, v};
        case 4:  return (led_color_t){t, p, v};
        default: return (led_color_t){v, p, q};
    }
}

led_color_t led_color_from_preset(led_color_preset_t preset) {
    switch (preset) {
        case LED_COLOR_PRESET_OFF:         return LED_COLOR_OFF;
        case LED_COLOR_PRESET_WHITE:       return LED_COLOR_WHITE;
        case LED_COLOR_PRESET_RED:         return LED_COLOR_RED;
        case LED_COLOR_PRESET_GREEN:       return LED_COLOR_GREEN;
        case LED_COLOR_PRESET_BLUE:        return LED_COLOR_BLUE;
        case LED_COLOR_PRESET_YELLOW:      return LED_COLOR_YELLOW;
        case LED_COLOR_PRESET_CYAN:        return LED_COLOR_CYAN;
        case LED_COLOR_PRESET_MAGENTA:     return LED_COLOR_MAGENTA;
        case LED_COLOR_PRESET_ORANGE:      return LED_COLOR_ORANGE;
        case LED_COLOR_PRESET_PURPLE:      return LED_COLOR_PURPLE;
        case LED_COLOR_PRESET_PINK:        return LED_COLOR_PINK;
        case LED_COLOR_PRESET_WARM_WHITE:  return LED_COLOR_WARM_WHITE;
        case LED_COLOR_PRESET_COOL_WHITE:  return LED_COLOR_COOL_WHITE;
        default:                           return LED_COLOR_OFF;
    }
}

led_color_t led_color_blend(led_color_t c1, led_color_t c2, uint8_t blend) {
    uint16_t blend_inv = 255 - blend;
    return (led_color_t){
        .r = clamp_u8((c1.r * blend_inv + c2.r * blend) / 255),
        .g = clamp_u8((c1.g * blend_inv + c2.g * blend) / 255),
        .b = clamp_u8((c1.b * blend_inv + c2.b * blend) / 255)
    };
}

led_color_t led_color_scale(led_color_t color, uint8_t scale) {
    return (led_color_t){
        .r = (uint8_t)((color.r * scale) / 255),
        .g = (uint8_t)((color.g * scale) / 255),
        .b = (uint8_t)((color.b * scale) / 255)
    };
}

led_color_t led_color_rainbow(uint8_t position) {
    // Rainbow color wheel: 0-85 red->green, 86-170 green->blue, 171-255 blue->red
    if (position < 85) {
        return (led_color_t){255 - position * 3, position * 3, 0};
    } else if (position < 170) {
        position -= 85;
        return (led_color_t){0, 255 - position * 3, position * 3};
    } else {
        position -= 170;
        return (led_color_t){position * 3, 0, 255 - position * 3};
    }
}
