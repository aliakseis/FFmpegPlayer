#pragma once

#include "ffmpegdecoder.h"

class VideoParseRunnable
{
    FFmpegDecoder* m_ffmpeg;

    bool getVideoPacket(AVPacket* packet);

public:
    explicit VideoParseRunnable(FFmpegDecoder* parent)
        : m_ffmpeg(parent)
    {}
    void operator() ();
};

