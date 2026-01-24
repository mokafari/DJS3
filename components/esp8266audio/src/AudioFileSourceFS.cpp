#include "AudioFileSourceFS.h"
#include "esp8266audio_compat.h"

AudioFileSourceFS::AudioFileSourceFS() {
    f = nullptr;
    fileSize = 0;
}

AudioFileSourceFS::AudioFileSourceFS(const char *filename) {
    f = nullptr;
    fileSize = 0;
    open(filename);
}

bool AudioFileSourceFS::open(const char *filename) {
    if (f) {
        fclose(f);
    }
    f = fopen(filename, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    return true;
}

AudioFileSourceFS::~AudioFileSourceFS() {
    close();
}

uint32_t AudioFileSourceFS::read(void *data, uint32_t len) {
    if (!f) {
        return 0;
    }
    return fread(data, 1, len, f);
}

bool AudioFileSourceFS::seek(int32_t pos, int dir) {
    if (!f) {
        return false;
    }
    int seek_dir;
    switch (dir) {
        case SEEK_SET:
            seek_dir = SEEK_SET;
            break;
        case SEEK_CUR:
            seek_dir = SEEK_CUR;
            break;
        case SEEK_END:
            seek_dir = SEEK_END;
            break;
        default:
            return false;
    }
    return fseek(f, pos, seek_dir) == 0;
}

bool AudioFileSourceFS::close() {
    if (f) {
        fclose(f);
        f = nullptr;
        fileSize = 0;
        return true;
    }
    return false;
}

bool AudioFileSourceFS::isOpen() {
    return f != nullptr;
}

uint32_t AudioFileSourceFS::getSize() {
    return fileSize;
}

uint32_t AudioFileSourceFS::getPos() {
    if (!f) {
        return 0;
    }
    return ftell(f);
}

