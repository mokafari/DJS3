#include "AudioOutputI2S.h"
#include "esp8266audio_compat.h"

#ifdef ESP32
#include "driver/i2s_std.h"
#include "esp_log.h"
#endif

AudioOutputI2S::AudioOutputI2S() {
    i2sOn = false;
    mono = false;
    lsb_justified = false;
    channels = 2;
    hertz = 44100;
#ifdef ESP32
    bclkPin = 26;
    wclkPin = 25;
    doutPin = 22;
    _useAPLL = false;
#else
    bclkPin = 26;
    wclkPin = 27;
    doutPin = 28;
#endif
    mclkPin = -1;
    SetGain(1.0);
    SetBuffers();
}

bool AudioOutputI2S::SetBuffers(int dmaBufferCount, int dmaBufferBytes) {
    if (i2sOn || (dmaBufferCount < 3) || (dmaBufferBytes & 3)) {
        return false;
    }
    _buffers = dmaBufferCount;
    _bufferWords = dmaBufferBytes / 4;
    return true;
}

#if SOC_CLK_APLL_SUPPORTED
bool AudioOutputI2S::SetUseAPLL() {
    if (i2sOn) {
        return false;
    }
    _useAPLL = true;
    return true;
}
#endif

bool AudioOutputI2S::SetPinout(int bclk, int wclk, int dout, int mclk) {
    if (i2sOn) {
        return false;
    }
    bclkPin = bclk;
    wclkPin = wclk;
    doutPin = dout;
    mclkPin = mclk;
    return true;
}

bool AudioOutputI2S::SetRate(int hz) {
    if (hertz == hz) {
        return true;
    }
    hertz = hz;
    if (i2sOn) {
        auto adj = AdjustI2SRate(hz);
#ifdef ESP32
        i2s_std_clk_config_t clk_cfg;
        clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)adj);
#if SOC_CLK_APLL_SUPPORTED
        clk_cfg.clk_src = _useAPLL ? i2s_clock_src_t::I2S_CLK_SRC_APLL : i2s_clock_src_t::I2S_CLK_SRC_DEFAULT;
#endif
        i2s_channel_disable(_tx_handle);
        i2s_channel_reconfig_std_clock(_tx_handle, &clk_cfg);
        i2s_channel_enable(_tx_handle);
#endif
    }
    return true;
}

bool AudioOutputI2S::SetChannels(int ch) {
    if ((ch < 1) || (ch > 2)) {
        return false;
    }
    this->channels = ch;
    return true;
}

bool AudioOutputI2S::SetOutputModeMono(bool m) {
    this->mono = m;
    return true;
}

bool AudioOutputI2S::SetLsbJustified(bool lsbJustified) {
    if (i2sOn) {
        return false;
    }
    this->lsb_justified = lsbJustified;
    return true;
}

bool AudioOutputI2S::begin() {
#ifdef ESP32
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = _buffers;
    chan_cfg.dma_frame_num = _bufferWords;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &_tx_handle, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE("AudioOutputI2S", "i2s_new_channel failed: %s (0x%x)", esp_err_to_name(ret), ret);
        fflush(stdout);
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hertz),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = mclkPin < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)mclkPin,
            .bclk = bclkPin < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)bclkPin,
            .ws = wclkPin < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)wclkPin,
            .dout = (gpio_num_t)doutPin,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
#if SOC_CLK_APLL_SUPPORTED
    std_cfg.clk_cfg.clk_src = _useAPLL ? i2s_clock_src_t::I2S_CLK_SRC_APLL : i2s_clock_src_t::I2S_CLK_SRC_DEFAULT;
#endif
    std_cfg.slot_cfg.bit_shift = !lsb_justified;
    ESP_LOGI("AudioOutputI2S", "I2S GPIO config: BCLK=%d, WS=%d, DOUT=%d, MCLK=%d", 
             bclkPin, wclkPin, doutPin, mclkPin);
    ret = i2s_channel_init_std_mode(_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE("AudioOutputI2S", "i2s_channel_init_std_mode failed: %s (0x%x)", esp_err_to_name(ret), ret);
        ESP_LOGE("AudioOutputI2S", "  BCLK: GPIO %d, LRCK: GPIO %d, DIN: GPIO %d", bclkPin, wclkPin, doutPin);
        fflush(stdout);
        i2s_del_channel(_tx_handle);
        return false;
    }

    int16_t a[2] = {0, 0};
    size_t written = 0;
    do {
        i2s_channel_preload_data(_tx_handle, (void*)a, sizeof(a), &written);
    } while (written);

    ret = i2s_channel_enable(_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE("AudioOutputI2S", "i2s_channel_enable failed: %s (0x%x)", esp_err_to_name(ret), ret);
        fflush(stdout);
        i2s_channel_disable(_tx_handle);
        i2s_del_channel(_tx_handle);
        return false;
    }
    i2sOn = true;
#else
    i2sOn = false;
#endif
    SetRate(hertz ? hertz : 44100);
    return i2sOn;
}

bool AudioOutputI2S::ConsumeSample(int16_t sample[2]) {
    if (!i2sOn) {
        return false;
    }

    int16_t ms[2];
    ms[0] = sample[0];
    ms[1] = sample[1];
    MakeSampleStereo16(ms);

    if (this->mono) {
        int32_t ttl = ms[LEFTCHANNEL] + ms[RIGHTCHANNEL];
        ms[LEFTCHANNEL] = ms[RIGHTCHANNEL] = (ttl >> 1) & 0xffff;
    }
#ifdef ESP32
    uint32_t s32;
    s32 = ((Amplify(ms[RIGHTCHANNEL])) << 16) | (Amplify(ms[LEFTCHANNEL]) & 0xffff);

    size_t i2s_bytes_written = sizeof(uint32_t);
    i2s_channel_write(_tx_handle, (const char*)&s32, sizeof(uint32_t), &i2s_bytes_written, 0);
    return i2s_bytes_written;
#else
    return false;
#endif
}

void AudioOutputI2S::flush() {
#ifdef ESP32
    int buffersize = _buffers * _bufferWords;
    int16_t samples[2] = {0x0, 0x0};
    for (int i = 0; i < buffersize; i++) {
        while (!ConsumeSample(samples)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
#endif
}

bool AudioOutputI2S::stop() {
    if (!i2sOn) {
        return true;
    }
#ifdef ESP32
    i2s_channel_disable(_tx_handle);
    i2s_del_channel(_tx_handle);
    _tx_handle = nullptr;
#endif
    i2sOn = false;
    return true;
}

AudioOutputI2S::~AudioOutputI2S() {
    stop();
}

