#pragma once

#include "mainwindow.h"
#include "videoplayer.h"

#include <QFrame>
#include <QPointer>

class VideoControl;
class VideoProgressBar;
class VideoWidget;


class VideoPlayerWidget;

inline VideoPlayerWidget* VideoPlayerWidgetInstance()
{
    if (auto mainWindow = getMainWindow()) {
        return mainWindow->getPlayer();
    }
    return nullptr;
}


class VideoPlayerWidget : public QFrame, public VideoPlayer
{
	Q_OBJECT
public:
	explicit VideoPlayerWidget(QWidget* parent = nullptr);
	~VideoPlayerWidget() override;

	void pauseVideo();
	void resumeVideo();

	void stopVideo(bool showDefaultImage = false);
	bool isPaused();
    void seekByPercent(float percent);

	VideoDisplay* getCurrentDisplay();
	VideoWidget* videoWidget() {return m_videoWidget;}

	void setProgressbar(VideoProgressBar* progressbar);
	void setControl(VideoControl* controlWidget);

	void setDefaultPreviewPicture();

	QString currentFilename() const;

    void updateLayout();

	void exitFullScreen();

	void playFile(const QString& fileName);

protected:
	void resizeEvent(QResizeEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	bool eventFilter(QObject* object, QEvent* event) override;

public slots:
	void playPauseButtonAction();

private slots:
	void setVideoFilename(const QString& fileName);

	void updateViewOnVideoStop(bool showDefaultImage = true);

	void onPlayingFinished();

signals:
	void fileReleased();

private:
    friend class VideoWidget;

	VideoControl* m_controls{nullptr};
	VideoProgressBar* m_progressBar{nullptr};
	QString m_currentFile;
	VideoWidget* m_videoWidget;
};
