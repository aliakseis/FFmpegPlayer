#pragma once

#include "ffmpegdecoder.h"

#include <stdint.h>
#include <vector>

class AudioParseRunnable
{
    FFmpegDecoder* m_ffmpeg;

    bool handlePacket(AVPacket packet, std::vector<uint8_t>& resampleBuffer);

public:
    explicit AudioParseRunnable(FFmpegDecoder* parent)
        : m_ffmpeg(parent)
    {}
    void operator() ();
};
