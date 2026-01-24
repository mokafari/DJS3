#ifndef _AUDIOFILESOURCEFS_H
#define _AUDIOFILESOURCEFS_H

#include "esp8266audio_compat.h"
#include "AudioFileSource.h"
#include <stdio.h>

class AudioFileSourceFS : public AudioFileSource {
public:
    AudioFileSourceFS();
    AudioFileSourceFS(const char *filename);
    virtual ~AudioFileSourceFS() override;

    virtual bool open(const char *filename) override;
    virtual uint32_t read(void *data, uint32_t len) override;
    virtual bool seek(int32_t pos, int dir) override;
    virtual bool close() override;
    virtual bool isOpen() override;
    virtual uint32_t getSize() override;
    virtual uint32_t getPos() override;

private:
    FILE *f;
    uint32_t fileSize;
};

#endif

