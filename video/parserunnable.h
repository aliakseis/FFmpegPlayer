#pragma once

#include "ffmpegdecoder.h"

class ParseRunnable
{
    FFmpegDecoder* m_ffmpeg;

    bool m_readerEOF;

    bool readFrame(AVPacket* packet);
    void sendSeekPacket();
    void fixDuration();

    void dispatchPacket(AVPacket& packet);

    void startAudioThread();
    void startVideoThread();

public:
    explicit ParseRunnable(FFmpegDecoder* parent) :
        m_ffmpeg(parent),
        m_readerEOF(false)
    {}
    void operator() ();
};

