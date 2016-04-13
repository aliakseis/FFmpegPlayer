#pragma once

#include "ffmpegdecoder.h"

class ParseRunnable
{
    FFmpegDecoder* m_ffmpeg;

    void seek();
    void fixDuration();

    void dispatchPacket(AVPacket& packet);

    void startAudioThread();
    void startVideoThread();

public:
    explicit ParseRunnable(FFmpegDecoder* parent) :
        m_ffmpeg(parent)
    {}
    void operator() ();
};

