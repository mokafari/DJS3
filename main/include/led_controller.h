/**
 * @file led_controller.h
 * @brief Comprehensive LED control for DJ deck
 * 
 * Features:
 * - Standard indicator LEDs (play, cue, sync, etc.)
 * - Beat flash synchronization with BPM
 * - WS2812/NeoPixel RGB strip support (via RMT peripheral)
 * - VU meter LED arrays
 * - Button illumination control
 * - Brightness control and dimming
 * - Animation patterns (chase, pulse, rainbow)
 * - Color presets and custom colors
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/** Maximum number of WS2812 LEDs per strip */
#define LED_WS2812_MAX_LEDS         120

/** Number of VU meter segments */
#define LED_VU_SEGMENTS             8

/** Animation frame rate (Hz) */
#define LED_ANIMATION_FPS           60

/** Beat flash duration (ms) */
#define LED_BEAT_FLASH_DURATION_MS  50

/* ============================================================================
 * LED Pin Configuration (override in board_config.h)
 * ============================================================================ */

#ifndef LED_PLAY_PIN
#define LED_PLAY_PIN                (-1)
#endif

#ifndef LED_CUE_PIN
#define LED_CUE_PIN                 (-1)
#endif

#ifndef LED_SYNC_PIN
#define LED_SYNC_PIN                (-1)
#endif

#ifndef LED_LOOP_PIN
#define LED_LOOP_PIN                (-1)
#endif

#ifndef LED_SLIP_PIN
#define LED_SLIP_PIN                (-1)
#endif

#ifndef LED_MASTER_PIN
#define LED_MASTER_PIN              (-1)
#endif

#ifndef LED_WS2812_PIN
#define LED_WS2812_PIN              (-1)
#endif

#ifndef LED_WS2812_COUNT
#define LED_WS2812_COUNT            0
#endif

#ifndef LED_VU_LEFT_PINS
#define LED_VU_LEFT_PINS            {-1,-1,-1,-1,-1,-1,-1,-1}
#endif

#ifndef LED_VU_RIGHT_PINS
#define LED_VU_RIGHT_PINS           {-1,-1,-1,-1,-1,-1,-1,-1}
#endif

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Standard indicator LED IDs
 */
typedef enum {
    LED_ID_PLAY = 0,
    LED_ID_CUE,
    LED_ID_SYNC,
    LED_ID_LOOP,
    LED_ID_SLIP,
    LED_ID_MASTER,
    LED_ID_HOT_CUE_1,
    LED_ID_HOT_CUE_2,
    LED_ID_HOT_CUE_3,
    LED_ID_HOT_CUE_4,
    LED_ID_HOT_CUE_5,
    LED_ID_HOT_CUE_6,
    LED_ID_HOT_CUE_7,
    LED_ID_HOT_CUE_8,
    LED_ID_COUNT
} led_id_t;

/**
 * @brief LED blink modes
 */
typedef enum {
    LED_BLINK_OFF = 0,          ///< LED off
    LED_BLINK_SOLID,            ///< LED solid on
    LED_BLINK_SLOW,             ///< Slow blink (1 Hz)
    LED_BLINK_FAST,             ///< Fast blink (4 Hz)
    LED_BLINK_BEAT,             ///< Flash on beat
    LED_BLINK_PULSE             ///< Smooth pulse (fade in/out)
} led_blink_mode_t;

/**
 * @brief RGB color structure
 */
typedef struct {
    uint8_t r;  ///< Red (0-255)
    uint8_t g;  ///< Green (0-255)
    uint8_t b;  ///< Blue (0-255)
} led_color_t;

/**
 * @brief Color preset IDs
 */
typedef enum {
    LED_COLOR_PRESET_OFF = 0,
    LED_COLOR_PRESET_WHITE,
    LED_COLOR_PRESET_RED,
    LED_COLOR_PRESET_GREEN,
    LED_COLOR_PRESET_BLUE,
    LED_COLOR_PRESET_YELLOW,
    LED_COLOR_PRESET_CYAN,
    LED_COLOR_PRESET_MAGENTA,
    LED_COLOR_PRESET_ORANGE,
    LED_COLOR_PRESET_PURPLE,
    LED_COLOR_PRESET_PINK,
    LED_COLOR_PRESET_WARM_WHITE,
    LED_COLOR_PRESET_COOL_WHITE,
    LED_COLOR_PRESET_COUNT
} led_color_preset_t;

/**
 * @brief Animation pattern types
 */
typedef enum {
    LED_ANIM_NONE = 0,          ///< No animation (static)
    LED_ANIM_CHASE,             ///< Chase pattern (running lights)
    LED_ANIM_PULSE,             ///< Pulsing brightness
    LED_ANIM_RAINBOW,           ///< Rainbow color cycle
    LED_ANIM_RAINBOW_CHASE,     ///< Rainbow chase pattern
    LED_ANIM_SPARKLE,           ///< Random sparkle
    LED_ANIM_BREATHE,           ///< Breathing effect
    LED_ANIM_FIRE,              ///< Fire flicker effect
    LED_ANIM_VU_METER,          ///< VU meter visualization
    LED_ANIM_BEAT_PULSE,        ///< Pulse on beat
    LED_ANIM_BEAT_STROBE,       ///< Strobe on beat
    LED_ANIM_WAVE,              ///< Sine wave pattern
    LED_ANIM_COUNT
} led_animation_t;

/**
 * @brief Strip zone definition
 */
typedef struct {
    uint16_t start_led;         ///< First LED index in zone
    uint16_t led_count;         ///< Number of LEDs in zone
    led_animation_t animation;  ///< Active animation
    led_color_t primary_color;  ///< Primary color
    led_color_t secondary_color;///< Secondary color (for patterns)
    uint8_t speed;              ///< Animation speed (1-255)
    uint8_t intensity;          ///< Intensity/brightness (0-255)
    bool reversed;              ///< Reverse direction
} led_strip_zone_t;

/**
 * @brief VU meter configuration
 */
typedef struct {
    uint8_t green_threshold;    ///< Threshold for green zone (0-100%)
    uint8_t yellow_threshold;   ///< Threshold for yellow zone (0-100%)
    uint8_t red_threshold;      ///< Threshold for red zone (0-100%)
    uint16_t peak_hold_ms;      ///< Peak hold time in ms (0-65535)
    uint8_t decay_rate;         ///< Decay rate (1-255)
    bool show_peak;             ///< Show peak indicator
} led_vu_config_t;

/**
 * @brief LED controller configuration
 */
typedef struct {
    uint8_t global_brightness;  ///< Global brightness (0-255)
    float bpm;                  ///< Current BPM for beat sync
    bool beat_sync_enabled;     ///< Enable beat synchronization
    led_vu_config_t vu_config;  ///< VU meter configuration
} led_controller_config_t;

/**
 * @brief Beat sync callback (called when beat detected)
 */
typedef void (*led_beat_callback_t)(float bpm, float phase, void *arg);

/* ============================================================================
 * Initialization and Core Functions
 * ============================================================================ */

/**
 * @brief Initialize LED controller
 * 
 * Sets up GPIO pins for standard LEDs and RMT peripheral for WS2812 strips.
 * 
 * @return true on success, false on failure
 */
bool led_controller_init(void);

/**
 * @brief Deinitialize LED controller
 * 
 * Turns off all LEDs and releases resources.
 */
void led_controller_deinit(void);

/**
 * @brief Update LED controller (call in main loop)
 * 
 * Updates animations, blink states, and refreshes WS2812 strip.
 * Should be called at least 60 times per second for smooth animations.
 */
void led_controller_update(void);

/**
 * @brief Set global brightness
 * 
 * @param brightness Brightness level (0-255)
 */
void led_controller_set_brightness(uint8_t brightness);

/**
 * @brief Get global brightness
 * 
 * @return Current brightness level (0-255)
 */
uint8_t led_controller_get_brightness(void);

/**
 * @brief Enable/disable LED controller
 * 
 * When disabled, all LEDs are off but state is preserved.
 * 
 * @param enabled true to enable, false to disable
 */
void led_controller_enable(bool enabled);

/**
 * @brief Check if LED controller is enabled
 * 
 * @return true if enabled
 */
bool led_controller_is_enabled(void);

/* ============================================================================
 * Standard Indicator LED Functions
 * ============================================================================ */

/**
 * @brief Set indicator LED state
 * 
 * @param led_id LED identifier
 * @param on true for on, false for off
 */
void led_set(led_id_t led_id, bool on);

/**
 * @brief Get indicator LED state
 * 
 * @param led_id LED identifier
 * @return true if on, false if off
 */
bool led_get(led_id_t led_id);

/**
 * @brief Set indicator LED blink mode
 * 
 * @param led_id LED identifier
 * @param mode Blink mode
 */
void led_set_blink_mode(led_id_t led_id, led_blink_mode_t mode);

/**
 * @brief Set all indicator LEDs
 * 
 * @param on true for on, false for off
 */
void led_set_all(bool on);

/**
 * @brief Toggle indicator LED
 * 
 * @param led_id LED identifier
 */
void led_toggle(led_id_t led_id);

/* ============================================================================
 * Beat Synchronization Functions
 * ============================================================================ */

/**
 * @brief Set BPM for beat synchronization
 * 
 * @param bpm Beats per minute (60-200)
 */
void led_set_bpm(float bpm);

/**
 * @brief Get current BPM
 * 
 * @return Current BPM
 */
float led_get_bpm(void);

/**
 * @brief Trigger beat flash
 * 
 * Manually triggers a beat flash on all LEDs configured for beat sync.
 */
void led_trigger_beat(void);

/**
 * @brief Set beat phase
 * 
 * Aligns the beat sync to a specific phase.
 * 
 * @param phase Phase value (0.0 to 1.0)
 */
void led_set_beat_phase(float phase);

/**
 * @brief Enable/disable beat synchronization
 * 
 * @param enabled true to enable, false to disable
 */
void led_set_beat_sync(bool enabled);

/**
 * @brief Register beat callback
 * 
 * @param callback Callback function
 * @param arg User argument
 */
void led_set_beat_callback(led_beat_callback_t callback, void *arg);

/* ============================================================================
 * WS2812 Strip Functions
 * ============================================================================ */

/**
 * @brief Set single WS2812 LED color
 * 
 * @param index LED index
 * @param color RGB color
 */
void led_strip_set_pixel(uint16_t index, led_color_t color);

/**
 * @brief Set range of WS2812 LEDs to same color
 * 
 * @param start Start index
 * @param count Number of LEDs
 * @param color RGB color
 */
void led_strip_set_range(uint16_t start, uint16_t count, led_color_t color);

/**
 * @brief Set all WS2812 LEDs to same color
 * 
 * @param color RGB color
 */
void led_strip_set_all(led_color_t color);

/**
 * @brief Clear all WS2812 LEDs (set to black)
 */
void led_strip_clear(void);

/**
 * @brief Refresh WS2812 strip (send data)
 * 
 * Normally called automatically by led_controller_update().
 */
void led_strip_refresh(void);

/**
 * @brief Get WS2812 LED count
 * 
 * @return Number of LEDs in strip
 */
uint16_t led_strip_get_count(void);

/* ============================================================================
 * Animation Functions
 * ============================================================================ */

/**
 * @brief Set strip animation
 * 
 * @param animation Animation type
 */
void led_strip_set_animation(led_animation_t animation);

/**
 * @brief Get current animation
 * 
 * @return Current animation type
 */
led_animation_t led_strip_get_animation(void);

/**
 * @brief Set animation speed
 * 
 * @param speed Speed (1-255, higher = faster)
 */
void led_strip_set_animation_speed(uint8_t speed);

/**
 * @brief Set animation primary color
 * 
 * @param color Primary color
 */
void led_strip_set_primary_color(led_color_t color);

/**
 * @brief Set animation secondary color
 * 
 * @param color Secondary color
 */
void led_strip_set_secondary_color(led_color_t color);

/**
 * @brief Pause animation
 */
void led_strip_pause_animation(void);

/**
 * @brief Resume animation
 */
void led_strip_resume_animation(void);

/**
 * @brief Reset animation to start
 */
void led_strip_reset_animation(void);

/* ============================================================================
 * Zone Functions (for multi-zone strip control)
 * ============================================================================ */

/**
 * @brief Configure a strip zone
 * 
 * @param zone_id Zone identifier (0-7)
 * @param zone Zone configuration
 * @return true on success
 */
bool led_strip_set_zone(uint8_t zone_id, const led_strip_zone_t *zone);

/**
 * @brief Get zone configuration
 * 
 * @param zone_id Zone identifier
 * @param zone Output zone configuration
 * @return true if zone exists
 */
bool led_strip_get_zone(uint8_t zone_id, led_strip_zone_t *zone);

/**
 * @brief Clear zone (disable it)
 * 
 * @param zone_id Zone identifier
 */
void led_strip_clear_zone(uint8_t zone_id);

/**
 * @brief Clear all zones
 */
void led_strip_clear_all_zones(void);

/* ============================================================================
 * VU Meter Functions
 * ============================================================================ */

/**
 * @brief Set VU meter level (left channel)
 * 
 * @param level Level (0.0 to 1.0)
 */
void led_vu_set_left(float level);

/**
 * @brief Set VU meter level (right channel)
 * 
 * @param level Level (0.0 to 1.0)
 */
void led_vu_set_right(float level);

/**
 * @brief Set VU meter levels (both channels)
 * 
 * @param left Left channel level (0.0 to 1.0)
 * @param right Right channel level (0.0 to 1.0)
 */
void led_vu_set_levels(float left, float right);

/**
 * @brief Configure VU meter
 * 
 * @param config VU meter configuration
 */
void led_vu_configure(const led_vu_config_t *config);

/**
 * @brief Enable/disable VU meter peak hold
 * 
 * @param enabled true to enable, false to disable
 */
void led_vu_set_peak_hold(bool enabled);

/* ============================================================================
 * Button Illumination Functions
 * ============================================================================ */

/**
 * @brief Set hot cue button color
 * 
 * @param hot_cue_num Hot cue number (1-8)
 * @param color Button color
 */
void led_set_hot_cue_color(uint8_t hot_cue_num, led_color_t color);

/**
 * @brief Set hot cue button from preset
 * 
 * @param hot_cue_num Hot cue number (1-8)
 * @param preset Color preset
 */
void led_set_hot_cue_preset(uint8_t hot_cue_num, led_color_preset_t preset);

/**
 * @brief Clear all hot cue button colors
 */
void led_clear_hot_cue_colors(void);

/* ============================================================================
 * Color Utility Functions
 * ============================================================================ */

/**
 * @brief Create RGB color
 * 
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @return led_color_t structure
 */
led_color_t led_color_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Create color from HSV
 * 
 * @param h Hue (0-359)
 * @param s Saturation (0-255)
 * @param v Value/brightness (0-255)
 * @return led_color_t structure
 */
led_color_t led_color_hsv(uint16_t h, uint8_t s, uint8_t v);

/**
 * @brief Get color from preset
 * 
 * @param preset Color preset
 * @return led_color_t structure
 */
led_color_t led_color_from_preset(led_color_preset_t preset);

/**
 * @brief Blend two colors
 * 
 * @param c1 First color
 * @param c2 Second color
 * @param blend Blend factor (0 = c1, 255 = c2)
 * @return Blended color
 */
led_color_t led_color_blend(led_color_t c1, led_color_t c2, uint8_t blend);

/**
 * @brief Scale color brightness
 * 
 * @param color Input color
 * @param scale Scale factor (0-255)
 * @return Scaled color
 */
led_color_t led_color_scale(led_color_t color, uint8_t scale);

/**
 * @brief Get rainbow color from position
 * 
 * @param position Position in rainbow (0-255)
 * @return Rainbow color at position
 */
led_color_t led_color_rainbow(uint8_t position);

/* ============================================================================
 * Preset Color Constants
 * ============================================================================ */

#define LED_COLOR_OFF           ((led_color_t){0, 0, 0})
#define LED_COLOR_WHITE         ((led_color_t){255, 255, 255})
#define LED_COLOR_RED           ((led_color_t){255, 0, 0})
#define LED_COLOR_GREEN         ((led_color_t){0, 255, 0})
#define LED_COLOR_BLUE          ((led_color_t){0, 0, 255})
#define LED_COLOR_YELLOW        ((led_color_t){255, 255, 0})
#define LED_COLOR_CYAN          ((led_color_t){0, 255, 255})
#define LED_COLOR_MAGENTA       ((led_color_t){255, 0, 255})
#define LED_COLOR_ORANGE        ((led_color_t){255, 128, 0})
#define LED_COLOR_PURPLE        ((led_color_t){128, 0, 255})
#define LED_COLOR_PINK          ((led_color_t){255, 105, 180})
#define LED_COLOR_WARM_WHITE    ((led_color_t){255, 200, 150})
#define LED_COLOR_COOL_WHITE    ((led_color_t){200, 200, 255})

#ifdef __cplusplus
}
#endif

#endif /* LED_CONTROLLER_H */
