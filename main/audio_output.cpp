/**
 * @file audio_output.cpp
 * @brief Audio output implementation for onboard NS4168 audio chip via I2S
 */

#include "audio_output.h"
#include "esp_log.h"
#include "AudioOutputI2S.h"
#include "driver/gpio.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_output";

#ifdef __cplusplus
extern "C" {
#endif

static AudioOutputI2S *audio_i2s = nullptr;
static uint32_t current_sample_rate = 44100;
static uint8_t current_channels = 2;

bool audio_output_init(void) {
    if (audio_i2s != nullptr) {
        ESP_LOGW(TAG, "Audio output already initialized");
        return true;
    }

#ifdef AUDIO_OUTPUT_DISABLE
    ESP_LOGI(TAG, "Audio output initialization disabled (AUDIO_OUTPUT_DISABLE defined)");
    return false;
#endif

    ESP_LOGI(TAG, "Initializing I2S audio output for onboard NS4168 audio chip");
    ESP_LOGI(TAG, "  BCLK: GPIO %d", I2S_BCLK_PIN);
    ESP_LOGI(TAG, "  LRCK: GPIO %d", I2S_LRCK_PIN);
    ESP_LOGI(TAG, "  DIN:  GPIO %d", I2S_DIN_PIN);

    // GPIO 2 (LRCK) may be affected during boot/reset
    // Ensure it's not being used by another peripheral and add delay for reset signals to settle
    // Note: I2S driver will configure pins, so we don't configure them as GPIO outputs here
    vTaskDelay(pdMS_TO_TICKS(50)); // Delay to allow any reset signals to settle
    
    ESP_LOGI(TAG, "Proceeding with I2S initialization...");

    audio_i2s = new AudioOutputI2S();
    if (!audio_i2s) {
        ESP_LOGE(TAG, "Failed to create AudioOutputI2S");
        return false;
    }

    // Configure pinout for onboard NS4168 audio chip
    if (!audio_i2s->SetPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DIN_PIN, I2S_MCLK_PIN)) {
        ESP_LOGE(TAG, "Failed to set I2S pinout");
        delete audio_i2s;
        audio_i2s = nullptr;
        return false;
    }

    // Set sample rate and channels
    audio_i2s->SetRate(current_sample_rate);
    audio_i2s->SetChannels(current_channels);

    // Initialize I2S
    ESP_LOGI(TAG, "Calling audio_i2s->begin()...");
    if (!audio_i2s->begin()) {
        ESP_LOGE(TAG, "Failed to begin I2S - check AudioOutputI2S logs above for details");
        // Force flush logs to ensure error messages are visible
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to allow log output
        delete audio_i2s;
        audio_i2s = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "audio_i2s->begin() succeeded");

    ESP_LOGI(TAG, "Audio output initialized: %d Hz, %d channels", 
             current_sample_rate, current_channels);
    return true;
}

void audio_output_deinit(void) {
    if (audio_i2s != nullptr) {
        audio_i2s->stop();
        delete audio_i2s;
        audio_i2s = nullptr;
        ESP_LOGI(TAG, "Audio output deinitialized");
    }
}

bool audio_output_set_rate(uint32_t sample_rate) {
    if (audio_i2s == nullptr) {
        ESP_LOGE(TAG, "Audio output not initialized");
        return false;
    }

    if (audio_i2s->SetRate(sample_rate)) {
        current_sample_rate = sample_rate;
        ESP_LOGI(TAG, "Sample rate set to %d Hz", sample_rate);
        return true;
    }
    return false;
}

uint32_t audio_output_get_rate(void) {
    return current_sample_rate;
}

bool audio_output_set_channels(uint8_t channels) {
    if (audio_i2s == nullptr) {
        ESP_LOGE(TAG, "Audio output not initialized");
        return false;
    }

    if (audio_i2s->SetChannels(channels)) {
        current_channels = channels;
        ESP_LOGI(TAG, "Channels set to %d", channels);
        return true;
    }
    return false;
}

bool audio_output_set_gain(float gain) {
    if (audio_i2s == nullptr) {
        ESP_LOGE(TAG, "Audio output not initialized");
        return false;
    }

    return audio_i2s->SetGain(gain);
}

#ifdef __cplusplus
}
#endif

// C++ function - must be outside extern "C" block
AudioOutputI2S* audio_output_get_i2s(void) {
    return audio_i2s;
}

