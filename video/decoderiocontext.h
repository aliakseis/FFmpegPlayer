#pragma once

#include <cstdint>
#include <memory>
#include <streambuf>

struct AVIOContext;
struct AVFormatContext;

class DecoderIOContext
{
private:
    AVIOContext *ioCtx;
    uint8_t *buffer;  // internal buffer for ffmpeg
    int bufferSize;
    std::unique_ptr<std::streambuf> stream;

public:
    DecoderIOContext(std::unique_ptr<std::streambuf> s);
    ~DecoderIOContext();

    void initAVFormatContext(AVFormatContext * /*pCtx*/);

    static int IOReadFunc(void *data, uint8_t *buf, int buf_size);
    static int64_t IOSeekFunc(void *data, int64_t pos, int whence);
};
