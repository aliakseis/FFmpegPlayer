#include "widgetdisplay.h"

#include <QDebug>

#include <utility>

WidgetDisplay::WidgetDisplay(QWidget* parent) : QLabel(parent)
{
    setScaledContents(true);
    connect(this, &WidgetDisplay::display, this, &WidgetDisplay::currentDisplay);
}

void WidgetDisplay::currentDisplay(unsigned int generation)
{
    m_display = QPixmap::fromImage(m_image);
    setPixmap(m_display);

    finishedDisplayingFrame(generation);
}


void WidgetDisplay::showPicture(const QImage& picture)
{
	showPicture(QPixmap::fromImage(picture));
}

void WidgetDisplay::showPicture(const QPixmap& picture)
{
	setPixmap(picture);
}


void WidgetDisplay::updateFrame(IFrameDecoder* decoder, unsigned int generation)
{
    FrameRenderingData data;
    if (!decoder->getFrameRenderingData(&data))
    {
        return;
    }

    m_aspectRatio = float(data.height) / data.width;

    m_image = QImage(data.image[0], data.width, data.height, data.pitch[0], QImage::Format_RGB888);
}

void WidgetDisplay::drawFrame(IFrameDecoder* decoder, unsigned int generation)
{
    emit display(generation);
}

void WidgetDisplay::decoderClosing()
{
}
