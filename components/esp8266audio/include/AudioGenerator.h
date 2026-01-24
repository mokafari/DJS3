#ifndef _AUDIOGENERATOR_H
#define _AUDIOGENERATOR_H

#include "esp8266audio_compat.h"
#include "AudioStatus.h"
#include "AudioFileSource.h"
#include "AudioOutput.h"

class AudioGenerator {
public:
    AudioGenerator() {
        lastSample[0] = 0;
        lastSample[1] = 0;
        running = false;
        file = nullptr;
        output = nullptr;
    };
    virtual ~AudioGenerator() {};
    virtual bool begin(AudioFileSource *source, AudioOutput *out) {
        (void)source;
        (void)out;
        return false;
    };
    virtual bool loop() {
        return false;
    };
    virtual bool stop() {
        return false;
    };
    virtual bool isRunning() {
        return false;
    };
    virtual void desync() { };

public:
    virtual bool RegisterMetadataCB(AudioStatus::metadataCBFn fn, void *data) {
        return cb.RegisterMetadataCB(fn, data);
    }
    virtual bool RegisterStatusCB(AudioStatus::statusCBFn fn, void *data) {
        return cb.RegisterStatusCB(fn, data);
    }

protected:
    bool running;
    AudioFileSource *file;
    AudioOutput *output;
    int16_t lastSample[2];

protected:
    AudioStatus cb;
};

#endif

