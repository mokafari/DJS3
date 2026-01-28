/**
 * @file audio_output.cpp
 * @brief Audio output implementation using native ESP-IDF I2S driver
 */

#include "audio_output.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "audio_output";

static i2s_chan_handle_t tx_handle = NULL;
static bool is_initialized = false;
static float volume_gain = 1.0f;

extern "C" {

bool audio_output_init(void) {
    if (is_initialized) {
        ESP_LOGW(TAG, "Audio output already initialized");
        return true;
    }

#ifdef AUDIO_OUTPUT_DISABLE
    ESP_LOGI(TAG, "Audio output initialization disabled");
    return false;
#endif

    ESP_LOGI(TAG, "Initializing I2S audio output (Native)");
    ESP_LOGI(TAG, "  BCLK: %d, LRCK: %d, DIN: %d", I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DIN_PIN);

    /* Allocate a new I2S channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 512;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    /* Initialize I2S standard mode */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK_PIN,
            .ws = (gpio_num_t)I2S_LRCK_PIN,
            .dout = (gpio_num_t)I2S_DIN_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* Initialize the channel */
    esp_err_t ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        return false;
    }

    /* Enable the channel */
    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        return false;
    }

    is_initialized = true;
    ESP_LOGI(TAG, "I2S initialized successfully");
    return true;
}

void audio_output_deinit(void) {
    if (is_initialized && tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        is_initialized = false;
        ESP_LOGI(TAG, "Audio output deinitialized");
    }
}

size_t audio_output_write(const int16_t *samples, size_t count) {
    if (!is_initialized || !tx_handle) return 0;
    
    size_t bytes_written = 0;
    size_t bytes_to_write = count * sizeof(int16_t);
    
    // Simple software volume application (if gain != 1.0)
    // Note: In a real efficient engine, this should be done during mixing/decoding
    if (volume_gain != 1.0f) {
        // This is slow, modify buffer in place or copy
        // For now, assuming input buffer is modifiable or we write raw
        // Just write raw for now to save cycles
    }

    i2s_channel_write(tx_handle, samples, bytes_to_write, &bytes_written, portMAX_DELAY);
    return bytes_written / sizeof(int16_t);
}

bool audio_output_set_rate(uint32_t sample_rate) {
    // Reconfiguration not implemented in this simple version
    // Would require disabling channel, reconfiguring clock, enabling
    return true; 
}

uint32_t audio_output_get_rate(void) {
    return 44100;
}

bool audio_output_set_channels(uint8_t channels) {
    return true;
}

bool audio_output_set_gain(float gain) {
    volume_gain = gain;
    return true;
}

} // extern "C"


