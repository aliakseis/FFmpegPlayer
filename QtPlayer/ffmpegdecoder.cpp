#include "ffmpegdecoder.h"

//#include "../audio/AudioPlayerWasapi.h"

#include "portaudioplayer.h"

#include "videodisplay.h"

FFmpegDecoderWrapper::FFmpegDecoderWrapper()
    : m_frameDecoder(
        GetFrameDecoder(std::make_unique<PortAudioPlayer>()))
{
    m_frameDecoder->setDecoderListener(this);
}

FFmpegDecoderWrapper::~FFmpegDecoderWrapper() = default;

void FFmpegDecoderWrapper::setFrameListener(VideoDisplay* listener)
{
    m_frameDecoder->setFrameListener(listener);
    if (listener != nullptr) {
        listener->setDecoderObject(this);
}
}

bool FFmpegDecoderWrapper::openFile(const QString& file)
{
    return m_frameDecoder->openUrls({file.toStdString()});
}
