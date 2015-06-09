#pragma once

#include "ffmpegdecoder.h"

class DisplayRunnable
{
    FFmpegDecoder* m_ffmpeg;

public:
    explicit DisplayRunnable(FFmpegDecoder* parent) : m_ffmpeg(parent)
    {}
    void operator () ();
};
