/**
 * @file hardware_helpers.h
 * @brief Hardware test helper functions for integration tests
 */

#ifndef HARDWARE_HELPERS_H
#define HARDWARE_HELPERS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio test helpers
bool test_audio_output_init(void);
bool test_generate_tone(uint16_t frequency, uint32_t duration_ms);
bool test_verify_i2s_output(void);
bool test_audio_get_waveform(uint8_t *buffer, size_t size);

// Display test helpers
bool test_display_init(void);
bool test_draw_color_bars(void);
bool test_verify_pixel(uint16_t x, uint16_t y, uint16_t expected_color);

// Storage test helpers
bool test_sd_card_mount(void);
bool test_create_test_file(const char* path, const char* content);
bool test_verify_file_exists(const char* path);

// Control test helpers
bool test_simulate_button_press(int button_pin);
bool test_read_encoder_position(int encoder_pin_a, int encoder_pin_b);

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_HELPERS_H */

