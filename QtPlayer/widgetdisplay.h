#pragma once

#include "videodisplay.h"

#include <QLabel>
#include <QPixmap>
#include <QImage>

class WidgetDisplay : public QLabel, public VideoDisplay
{
	Q_OBJECT
public:
	WidgetDisplay(QWidget* parent = 0);
    ~WidgetDisplay() {}
    void displayFrame(unsigned int generation) override;

	void showPicture(const QImage& picture) override;
	void showPicture(const QPixmap& picture) override;

    void updateFrame(IFrameDecoder* decoder) override;
    void drawFrame(IFrameDecoder* decoder, unsigned int generation) override; // decoder->finishedDisplayingFrame() must be called
    void decoderClosing() override;

    float aspectRatio() const { return m_aspectRatio; }

protected:
	QImage m_image;
	QPixmap m_display;
    float m_aspectRatio { 0.75f };

protected slots:
    virtual void currentDisplay(unsigned int generation);
signals:
    void display(unsigned int generation);
};
