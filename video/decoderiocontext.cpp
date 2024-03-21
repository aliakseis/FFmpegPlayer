#include "decoderiocontext.h"

extern "C"
{
#include <libavformat/avformat.h>
}


// static
int DecoderIOContext::IOReadFunc(void *data, uint8_t *buf, int buf_size)
{
    auto *hctx = static_cast<DecoderIOContext*>(data);
    auto len = hctx->stream->sgetn((char *)buf, buf_size);
    if (len <= 0)
    {
        // Let FFmpeg know that we have reached EOF, or do something else
        return AVERROR_EOF;
    }
    return static_cast<int>(len);
}

// whence: SEEK_SET, SEEK_CUR, SEEK_END (like fseek) and AVSEEK_SIZE
// static
int64_t DecoderIOContext::IOSeekFunc(void *data, int64_t pos, int whence)
{
    auto *hctx = static_cast<DecoderIOContext*>(data);

    if (whence == AVSEEK_SIZE)
    {
        // return the file size if you wish to
        auto current = hctx->stream->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
        auto result = hctx->stream->pubseekoff(0, std::ios_base::end, std::ios_base::in);
        hctx->stream->pubseekoff(current, std::ios_base::beg, std::ios_base::in);
        return result;
    }

    std::ios_base::seekdir dir;
    switch (whence)
    {
    case SEEK_SET:
        dir = std::ios_base::beg;
        break;
    case SEEK_CUR:
        dir = std::ios_base::cur;
        break;
    case SEEK_END:
        dir = std::ios_base::end;
        break;
    default:
        return -1LL;
    }

    return hctx->stream->pubseekoff(pos, dir);
}

DecoderIOContext::DecoderIOContext(std::unique_ptr<std::streambuf> s)
    : stream(std::move(s))
{
    // allocate buffer
    bufferSize = 1024 * 64;                                  // FIXME: not sure what size to use
    buffer = static_cast<uint8_t*>(av_malloc(bufferSize));  // see destructor for details

    // allocate the AVIOContext
    ioCtx =
        avio_alloc_context(buffer, bufferSize,  // internal buffer and its size
                           0,                   // write flag (1=true,0=false)
                           (void *)this,  // user data, will be passed to our callback functions
                           IOReadFunc,
                           nullptr,  // no writing
                           IOSeekFunc);
}

DecoderIOContext::~DecoderIOContext()
{
    //CHANNEL_LOG(ffmpeg_closing) << "In DecoderIOContext::~DecoderIOContext()";

    // NOTE: ffmpeg messes up the buffer
    // so free the buffer first then free the context
    av_free(ioCtx->buffer);
    ioCtx->buffer = nullptr;
    av_free(ioCtx);
}

void DecoderIOContext::initAVFormatContext(AVFormatContext *pCtx)
{
    pCtx->pb = ioCtx;
    pCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    // you can specify a format directly
    // pCtx->iformat = av_find_input_format("h264");

    // or read some of the file and let ffmpeg do the guessing
    auto len = stream->sgetn((char *)buffer, bufferSize);
    if (len <= 0)
    {
        return;
    }
    // reset to beginning of file
    stream->pubseekoff(0, std::ios_base::beg, std::ios_base::in);

    AVProbeData probeData = {nullptr};
    probeData.buf = buffer;
    probeData.buf_size = bufferSize - 1;
    probeData.filename = "";
    pCtx->iformat = av_probe_input_format(&probeData, 1);
}
