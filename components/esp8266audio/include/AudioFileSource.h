#ifndef _AUDIOFILESOURCE_H
#define _AUDIOFILESOURCE_H

#include "esp8266audio_compat.h"
#include "AudioStatus.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

class AudioFileSource {
public:
    AudioFileSource() {};
    virtual ~AudioFileSource() {};
    virtual bool open(const char *filename) {
        (void)filename;
        return false;
    };
    virtual uint32_t read(void *data, uint32_t len) {
        (void)data;
        (void)len;
        return 0;
    };
    virtual uint32_t readNonBlock(void *data, uint32_t len) {
        return read(data, len);
    };
    virtual bool seek(int32_t pos, int dir) {
        (void)pos;
        (void)dir;
        return false;
    };
    virtual bool close() {
        return false;
    };
    virtual bool isOpen() {
        return false;
    };
    virtual uint32_t getSize() {
        return 0;
    };
    virtual uint32_t getPos() {
        return 0;
    };
    virtual bool loop() {
        return true;
    };

public:
    virtual bool RegisterMetadataCB(AudioStatus::metadataCBFn fn, void *data) {
        return cb.RegisterMetadataCB(fn, data);
    }
    virtual bool RegisterStatusCB(AudioStatus::statusCBFn fn, void *data) {
        return cb.RegisterStatusCB(fn, data);
    }

protected:
    AudioStatus cb;
};

#endif

