#pragma once

#include "AudioOutput.h"

#ifdef ESP32
#include "driver/i2s_std.h"
#endif

class AudioOutputI2S : public AudioOutput {
public:
    AudioOutputI2S();
    virtual ~AudioOutputI2S() override;
    
    bool SetBuffers(int dmaBufferCount = 5, int dmaBufferBytes = 2304);
    bool SetPinout(int bclkPin, int wclkPin, int doutPin, int mclkPin = -1);

#if defined(ESP32) && SOC_CLK_APLL_SUPPORTED
    bool SetUseAPLL();
#endif

    virtual bool SetRate(int hz) override;
    virtual bool SetChannels(int channels) override;
    virtual bool begin() override;
    virtual bool ConsumeSample(int16_t sample[2]) override;
    virtual void flush() override;
    virtual bool stop() override;
    
    bool SetOutputModeMono(bool mono);
    bool SetLsbJustified(bool lsbJustified);

protected:
    virtual int AdjustI2SRate(int hz) {
        return hz;
    }
    bool mono;
    bool lsb_justified;
    bool i2sOn;

    int8_t bclkPin;
    int8_t wclkPin;
    int8_t doutPin;
    int8_t mclkPin;

    size_t _buffers;
    size_t _bufferWords;

#ifdef ESP32
    bool _useAPLL;
    i2s_chan_handle_t _tx_handle;
#endif
};

