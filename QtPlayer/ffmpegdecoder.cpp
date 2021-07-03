#include "ffmpegdecoder.h"

#include "../audio/AudioPlayerWasapi.h"

#include "videodisplay.h"

FFmpegDecoderWrapper::FFmpegDecoderWrapper()
    : m_frameDecoder(
        GetFrameDecoder(std::make_unique<AudioPlayerWasapi>()))
{
    m_frameDecoder->setDecoderListener(this);
}

FFmpegDecoderWrapper::~FFmpegDecoderWrapper() = default;

void FFmpegDecoderWrapper::setFrameListener(VideoDisplay* listener)
{
    m_frameDecoder->setFrameListener(listener);
    if (listener)
        listener->setDecoderObject(this);
}

void FFmpegDecoderWrapper::openFile(QString file)
{
    m_frameDecoder->openUrls({file.toStdString()});
}

