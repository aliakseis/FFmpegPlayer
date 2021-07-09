#include "widgetdisplay.h"

#include <QDebug>

#include <utility>

WidgetDisplay::WidgetDisplay(QWidget* parent) : QLabel(parent)
{
    connect(this, &WidgetDisplay::display, this, &WidgetDisplay::currentDisplay);//, Qt::BlockingQueuedConnection);
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


void WidgetDisplay::updateFrame(IFrameDecoder* decoder)
{
    FrameRenderingData data;
    if (!decoder->getFrameRenderingData(&data))
    {
        return;
    }

    m_aspectRatio = float(data.height) / data.width;

    auto image = QImage(data.image[0], data.width, data.height, data.pitch[0], QImage::Format_RGB888);
    if (m_scrWidth > 0 && m_scrHeight > 0) {
        m_image = image.scaled(m_scrWidth, m_scrHeight);
    } else {
        m_image = std::move(image);
}
}

void WidgetDisplay::drawFrame(IFrameDecoder* decoder, unsigned int generation)
{
    emit display(generation);
}

void WidgetDisplay::decoderClosing()
{
}
