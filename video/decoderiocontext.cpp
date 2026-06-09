#include "decoderiocontext.h"

extern "C" {
#include <libavformat/avformat.h>
}

// static
int DecoderIOContext::IOReadFunc(void* data, uint8_t* buf, int buf_size)
{
    auto* hctx = static_cast<DecoderIOContext*>(data);
    try
    {
        std::streamsize len = hctx->stream->sgetn(reinterpret_cast<char*>(buf),
            static_cast<std::streamsize>(buf_size));
        if (len == 0)
        {
            return AVERROR_EOF; // assume that source is seekable
        }
        if (len < 0)
        {
            return AVERROR(EIO);
        }
        return static_cast<int>(len);
    }
    catch (const std::exception&)
    {
        return AVERROR(EIO);
    }
}

// static
int64_t DecoderIOContext::IOSeekFunc(void* data, int64_t pos, int whence)
{
    auto* hctx = static_cast<DecoderIOContext*>(data);

    if (whence == AVSEEK_SIZE)
    {
        auto current = hctx->stream->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
        auto endPos = hctx->stream->pubseekoff(0, std::ios_base::end, std::ios_base::in);
        hctx->stream->pubseekoff(current, std::ios_base::beg, std::ios_base::in);

        if (endPos == std::streampos(std::streamoff(-1)))
            return -1;

        return static_cast<int64_t>(endPos);
    }

    std::ios_base::seekdir dir;
    switch (whence)
    {
    case SEEK_SET: dir = std::ios_base::beg; break;
    case SEEK_CUR: dir = std::ios_base::cur; break;
    case SEEK_END: dir = std::ios_base::end; break;
    default:
        return -1;
    }

    auto newPos = hctx->stream->pubseekoff(static_cast<std::streamoff>(pos), dir,
        std::ios_base::in);
    if (newPos == std::streampos(std::streamoff(-1)))
        return -1;

    return static_cast<int64_t>(newPos);
}

DecoderIOContext::DecoderIOContext(std::unique_ptr<std::streambuf> s)
    : stream(std::move(s))
{
    bufferSize = 1024 * 64;
    buffer = static_cast<uint8_t*>(av_malloc(bufferSize));

    ioCtx = avio_alloc_context(
        buffer,
        bufferSize,
        0,                  // read-only
        this,               // opaque
        IOReadFunc,
        nullptr,            // no write
        IOSeekFunc
    );
}

DecoderIOContext::~DecoderIOContext()
{
    if (ioCtx)
    {
        av_freep(&ioCtx->buffer);
        av_freep(&ioCtx);
    }
}

void DecoderIOContext::initAVFormatContext(AVFormatContext* pCtx)
{
    pCtx->pb = ioCtx;
    pCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
}
