#pragma once

#include <QImage>
#include <QPixmap>
#include "ffmpegdecoder.h"

class VideoDisplay : public IFrameListener
{
public:
	VideoDisplay();
	~VideoDisplay() override;
    void finishedDisplayingFrame(unsigned int generation);
    void setDecoderObject(FFmpegDecoderWrapper* decoder);

    virtual void showPicture(const QImage& picture) = 0;
    virtual void showPicture(const QPixmap& picture) = 0;

    void setPreferredSize(int scrWidth, int scrHeight)
    {
        m_scrWidth = scrWidth;
        m_scrHeight = scrHeight;
    }

protected:
    FFmpegDecoderWrapper* m_decoder{nullptr};
    int m_scrWidth{};
    int m_scrHeight{};
};
