#include "AudioGeneratorMP3.h"
#include "esp8266audio_compat.h"
#include "libmad/config.h"
#include "libmad/mad.h"

AudioGeneratorMP3::AudioGeneratorMP3() {
    running = false;
    file = NULL;
    output = NULL;
    buff = NULL;
    synth = NULL;
    frame = NULL;
    stream = NULL;
    nsCountMax = 1152 / 32;
    madInitted = false;
    preallocateSpace = nullptr;
    preallocateSize = 0;
    preallocateStreamSpace = nullptr;
    preallocateStreamSize = 0;
    preallocateFrameSpace = nullptr;
    preallocateFrameSize = 0;
    preallocateSynthSpace = nullptr;
    preallocateSynthSize = 0;
}

AudioGeneratorMP3::AudioGeneratorMP3(void *space, int size): preallocateSpace(space), preallocateSize(size) {
    running = false;
    file = NULL;
    output = NULL;
    buff = NULL;
    synth = NULL;
    frame = NULL;
    stream = NULL;
    nsCountMax = 1152 / 32;
    madInitted = false;
    preallocateStreamSpace = nullptr;
    preallocateStreamSize = 0;
    preallocateFrameSpace = nullptr;
    preallocateFrameSize = 0;
    preallocateSynthSpace = nullptr;
    preallocateSynthSize = 0;
}

AudioGeneratorMP3::AudioGeneratorMP3(void *buff, int buffSize, void *stream, int streamSize, void *frame, int frameSize, void *synth, int synthSize):
    preallocateSpace(buff), preallocateSize(buffSize),
    preallocateStreamSpace(stream), preallocateStreamSize(streamSize),
    preallocateFrameSpace(frame), preallocateFrameSize(frameSize),
    preallocateSynthSpace(synth), preallocateSynthSize(synthSize) {
    running = false;
    file = NULL;
    output = NULL;
    this->buff = NULL;
    this->synth = NULL;
    this->frame = NULL;
    this->stream = NULL;
    nsCountMax = 1152 / 32;
    madInitted = false;
}

AudioGeneratorMP3::~AudioGeneratorMP3() {
    if (!preallocateSpace) {
        free(buff);
        free(synth);
        free(frame);
        free(stream);
    }
}

bool AudioGeneratorMP3::stop() {
    if (madInitted) {
        mad_synth_finish(synth);
        mad_frame_finish(frame);
        mad_stream_finish(stream);
        madInitted = false;
    }

    if (!preallocateSpace) {
        free(buff);
        free(synth);
        free(frame);
        free(stream);
    }

    buff = NULL;
    synth = NULL;
    frame = NULL;
    stream = NULL;

    running = false;
    if (output) {
        output->stop();
    }
    if (file) {
        return file->close();
    }
    return true;
}

bool AudioGeneratorMP3::isRunning() {
    return running;
}

enum mad_flow AudioGeneratorMP3::ErrorToFlow() {
    char err[64];
    char errLine[128];

    if ((lastReadPos == 0) && (stream->error == MAD_ERROR_LOSTSYNC)) {
        return MAD_FLOW_CONTINUE;
    }

    strcpy(err, mad_stream_errorstr(stream));
    snprintf(errLine, sizeof(errLine), "Decoding error '%s' at byte offset %d",
               err, (int)(stream->this_frame - buff) + lastReadPos);
    vTaskDelay(1); // Yield
    cb.st(stream->error, errLine);
    return MAD_FLOW_CONTINUE;
}

enum mad_flow AudioGeneratorMP3::Input() {
    int unused = 0;

    if (stream->next_frame) {
        unused = lastBuffLen - (stream->next_frame - buff);
        if (unused < 0) {
            desync();
            unused = 0;
        } else {
            memmove(buff, stream->next_frame, unused);
        }
        stream->next_frame = NULL;
    }

    if (unused == lastBuffLen) {
        unused = 0;
    }

    bool foundHeader = false;
    do {
        lastReadPos = file->getPos() - unused;
        int len = buffLen - unused;
        len = file->read(buff + unused, len);
        if ((len == 0) && (unused == 0)) {
            return MAD_FLOW_STOP;
        }
        if (len < 0) {
            desync();
            unused = 0;
        }

        lastBuffLen = len + unused;
        for (int i = 0; i < lastBuffLen - 1; i++) {
            if ((buff[i] == 0xff) && ((buff[i + 1] & 0xe0) == 0xe0)) {
                if (i) {
                    memmove(buff, buff + i, lastBuffLen - i);
                    lastBuffLen -= i;
                }
                foundHeader = true;
                break;
            }
        }
        if (!foundHeader) {
            unused = 0;
        } else {
            len = file->read(buff + lastBuffLen, buffLen - lastBuffLen);
            if (len < 0) {
                desync();
                unused = 0;
            }
            lastBuffLen += len;
            if (lastBuffLen < 8) {
                return MAD_FLOW_STOP;
            }
        }
    } while (!foundHeader);

    mad_stream_buffer(stream, buff, lastBuffLen);
    return MAD_FLOW_CONTINUE;
}

void AudioGeneratorMP3::desync() {
    if (audioLogger) {
        audioLogger->print("MP3:desync\n");
    }
    if (stream) {
        stream->next_frame = nullptr;
        stream->this_frame = nullptr;
        stream->sync = 0;
    }
    lastBuffLen = 0;
}

bool AudioGeneratorMP3::DecodeNextFrame() {
    if (mad_frame_decode(frame, stream) == -1) {
        ErrorToFlow();
        return false;
    }
    nsCountMax = MAD_NSBSAMPLES(&frame->header);
    return true;
}

bool AudioGeneratorMP3::GetOneSample(int16_t sample[2]) {
    if (synth->pcm.samplerate != lastRate) {
        output->SetRate(synth->pcm.samplerate);
        lastRate = synth->pcm.samplerate;
    }
    if (synth->pcm.channels != lastChannels) {
        output->SetChannels(synth->pcm.channels);
        lastChannels = synth->pcm.channels;
    }

    if (samplePtr < synth->pcm.length) {
        sample[AudioOutput::LEFTCHANNEL] = synth->pcm.samples[0][samplePtr];
        sample[AudioOutput::RIGHTCHANNEL] = synth->pcm.samples[1][samplePtr];
        samplePtr++;
    } else {
        samplePtr = 0;

        switch (mad_synth_frame_onens(synth, frame, nsCount++)) {
        case MAD_FLOW_STOP:
        case MAD_FLOW_BREAK:
            return false;
        default:
            break;
        }
        sample[AudioOutput::LEFTCHANNEL] = synth->pcm.samples[0][samplePtr];
        sample[AudioOutput::RIGHTCHANNEL] = synth->pcm.samples[1][samplePtr];
        samplePtr++;
    }
    return true;
}

bool AudioGeneratorMP3::loop() {
    if (!running) {
        goto done;
    }

    if (!output->ConsumeSample(lastSample)) {
        goto done;
    }

    do {
        if ((samplePtr >= synth->pcm.length) && (nsCount >= nsCountMax)) {
retry:
            if (Input() == MAD_FLOW_STOP) {
                return false;
            }

            if (!DecodeNextFrame()) {
                if (stream->error == MAD_ERROR_BUFLEN) {
                    if (++unrecoverable >= 3) {
                        unrecoverable = 0;
                        stop();
                        return running;
                    }
                } else {
                    unrecoverable = 0;
                }
                goto retry;
            }
            samplePtr = 9999;
            nsCount = 0;
        }

        if (!GetOneSample(lastSample)) {
            running = false;
            goto done;
        }
        if (lastChannels == 1) {
            lastSample[1] = lastSample[0];
        }
    } while (running && output->ConsumeSample(lastSample));

done:
    if (file) {
        file->loop();
    }
    if (output) {
        output->loop();
    }
    return running;
}

bool AudioGeneratorMP3::begin(AudioFileSource *source, AudioOutput *out) {
    if (!source) {
        return false;
    }
    file = source;
    if (!out) {
        return false;
    }
    this->output = out;
    if (!file->isOpen()) {
        return false;
    }

    unrecoverable = 0;
    output->SetChannels(2);

    if (!output->begin()) {
        return false;
    }

    samplePtr = 9999;
    nsCount = 9999;
    lastRate = 0;
    lastChannels = 0;
    lastReadPos = 0;
    lastBuffLen = 0;

    if (preallocateStreamSize + preallocateFrameSize + preallocateSynthSize) {
        if (preallocateSize >= preAllocBuffSize() &&
                preallocateStreamSize >= preAllocStreamSize() &&
                preallocateFrameSize >= preAllocFrameSize() &&
                preallocateSynthSize >= preAllocSynthSize()) {
            buff = reinterpret_cast<unsigned char *>(preallocateSpace);
            stream = reinterpret_cast<struct mad_stream *>(preallocateStreamSpace);
            frame = reinterpret_cast<struct mad_frame *>(preallocateFrameSpace);
            synth = reinterpret_cast<struct mad_synth *>(preallocateSynthSpace);
        } else {
            output->stop();
            return false;
        }
    } else if (preallocateSpace) {
        uint8_t *p = reinterpret_cast<uint8_t *>(preallocateSpace);
        buff = reinterpret_cast<unsigned char *>(p);
        p += preAllocBuffSize();
        stream = reinterpret_cast<struct mad_stream *>(p);
        p += preAllocStreamSize();
        frame = reinterpret_cast<struct mad_frame *>(p);
        p += preAllocFrameSize();
        synth = reinterpret_cast<struct mad_synth *>(p);
        p += preAllocSynthSize();
        int neededBytes = p - reinterpret_cast<uint8_t *>(preallocateSpace);
        if (neededBytes > preallocateSize) {
            output->stop();
            return false;
        }
    } else {
        buff = reinterpret_cast<unsigned char *>(malloc(buffLen));
        stream = reinterpret_cast<struct mad_stream *>(malloc(sizeof(struct mad_stream)));
        frame = reinterpret_cast<struct mad_frame *>(malloc(sizeof(struct mad_frame)));
        synth = reinterpret_cast<struct mad_synth *>(malloc(sizeof(struct mad_synth)));
        if (!buff || !stream || !frame || !synth) {
            free(buff);
            free(stream);
            free(frame);
            free(synth);
            buff = NULL;
            stream = NULL;
            frame = NULL;
            synth = NULL;
            output->stop();
            return false;
        }
    }

    mad_stream_init(stream);
    mad_frame_init(frame);
    mad_synth_init(synth);
    synth->pcm.length = 0;
    mad_stream_options(stream, 0);
    madInitted = true;

    running = true;
    return true;
}


// ESP32 stack checking helpers - implemented in separate file
