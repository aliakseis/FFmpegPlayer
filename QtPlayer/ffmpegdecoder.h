#pragma once

#include "../video/decoderinterface.h"

#include <QObject>

#include <QString>

#include <memory>

class VideoDisplay;

class FFmpegDecoderWrapper : public QObject, public FrameDecoderListener
{
	Q_OBJECT

public:
    FFmpegDecoderWrapper();
    ~FFmpegDecoderWrapper();

    FFmpegDecoderWrapper(const FFmpegDecoderWrapper&) = delete;
    FFmpegDecoderWrapper& operator=(const FFmpegDecoderWrapper&) = delete;

    void setFrameListener(VideoDisplay* listener);

    void openFile(const QString& file);
    void play(bool isPaused = false) { m_frameDecoder->play(); }
    bool pauseResume() { return m_frameDecoder->pauseResume(); }
    void close(bool isBlocking = true) { m_frameDecoder->close(); }
    void setVolume(double volume)
    {
        m_frameDecoder->setVolume(volume);
        emit volumeChanged(volume);
    }
    bool seekByPercent(float percent) { return m_frameDecoder->seekByPercent(percent); }

    double volume() const { return m_frameDecoder->volume(); }
    bool isPlaying() const { return m_frameDecoder->isPlaying(); }

    void finishedDisplayingFrame(unsigned int generation) { m_frameDecoder->finishedDisplayingFrame(generation); }

    IFrameDecoder* getFrameDecoder() const { return m_frameDecoder.get(); }

    void playingFinished() override { emit onPlayingFinished(); }
    virtual void changedFramePosition(
        long long start, long long frame, long long total) override
    {
        emit onChangedFramePosition(frame - start, total - start);
    }

signals:
    void onPlayingFinished();
    void onChangedFramePosition(qint64, qint64);
    void volumeChanged(double);

private:
    std::unique_ptr<IFrameDecoder> m_frameDecoder;
};
