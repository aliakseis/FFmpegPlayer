#include "videodisplay.h"

VideoDisplay::VideoDisplay() : m_decoder(nullptr)
{

}

VideoDisplay::~VideoDisplay()
{
    if (m_decoder)
        m_decoder->setFrameListener(nullptr);
}

void VideoDisplay::setDecoderObject(FFmpegDecoderWrapper* decoder)
{
    Q_ASSERT(decoder != nullptr);
    if (m_decoder != decoder)
    {
        m_decoder = decoder;
        m_decoder->setFrameListener(this);
        m_decoder->getFrameDecoder()->SetFrameFormat(IFrameDecoder::PIX_FMT_RGB24, false);
    }
}

void VideoDisplay::displayFrame(unsigned int generation)
{
    m_decoder->finishedDisplayingFrame(generation);
}
