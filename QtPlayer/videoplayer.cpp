#include "videoplayer.h"
#include "widgetdisplay.h"

#include <QDebug>

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer()
{
	delete m_display;
}

FFmpegDecoderWrapper* VideoPlayer::getDecoder()
{
    return &m_decoder;
}

VideoDisplay* VideoPlayer::getCurrentDisplay()
{
	return m_display;
}

void VideoPlayer::setDisplay(VideoDisplay* display)
{
	Q_ASSERT(display);
    delete m_display;

	m_display = display;
    m_decoder.setFrameListener(m_display);
}

void VideoPlayer::setState(VideoState newState)
{
	qDebug() << __FUNCTION__ << "newState:" << newState;
	m_state = newState;
}
