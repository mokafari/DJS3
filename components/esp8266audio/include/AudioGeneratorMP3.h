#ifndef _AUDIOGENERATORMP3_H
#define _AUDIOGENERATORMP3_H

#include "AudioGenerator.h"
#include "libmad/config.h"
#include "libmad/mad.h"

class AudioGeneratorMP3 : public AudioGenerator {
public:
    AudioGeneratorMP3();
    AudioGeneratorMP3(void *preallocateSpace, int preallocateSize);
    AudioGeneratorMP3(void *buff, int buffSize, void *stream, int streamSize, void *frame, int frameSize, void *synth, int synthSize);
    virtual ~AudioGeneratorMP3() override;
    virtual bool begin(AudioFileSource *source, AudioOutput *output) override;
    virtual bool loop() override;
    virtual bool stop() override;
    virtual bool isRunning() override;
    virtual void desync() override;

    static constexpr int preAllocSize() {
        return preAllocBuffSize() + preAllocStreamSize() + preAllocFrameSize() + preAllocSynthSize();
    }
    static constexpr int preAllocBuffSize() {
        return ((buffLen + 7) & ~7);
    }
    static constexpr int preAllocStreamSize() {
        return ((sizeof(struct mad_stream) + 7) & ~7);
    }
    static constexpr int preAllocFrameSize() {
        return (sizeof(struct mad_frame) + 7) & ~7;
    }
    static constexpr int preAllocSynthSize() {
        return (sizeof(struct mad_synth) + 7) & ~7;
    }

protected:
    void *preallocateSpace = nullptr;
    int preallocateSize = 0;
    void *preallocateStreamSpace = nullptr;
    int preallocateStreamSize = 0;
    void *preallocateFrameSpace = nullptr;
    int preallocateFrameSize = 0;
    void *preallocateSynthSpace = nullptr;
    int preallocateSynthSize = 0;

    static constexpr int buffLen = 0x600;
    unsigned char *buff;
    int lastReadPos;
    int lastBuffLen;
    unsigned int lastRate;
    int lastChannels;

    bool madInitted;
    struct mad_stream *stream;
    struct mad_frame *frame;
    struct mad_synth *synth;
    int samplePtr;
    int nsCount;
    int nsCountMax;

    enum mad_flow ErrorToFlow();
    enum mad_flow Input();
    bool DecodeNextFrame();
    bool GetOneSample(int16_t sample[2]);

private:
    int unrecoverable = 0;
};

#endif

