#pragma once

#include "mainwindow.h"
#include "videowidget.h"
#include "videoplayer.h"

#include <QFrame>
#include <QPointer>

class VideoControl;
class VideoProgressBar;
class VideoWidget;
class Spinner;


class VideoPlayerWidget;

inline VideoPlayerWidget* VideoPlayerWidgetInstance()
{
    if (auto mainWindow = getMainWindow())
        return mainWindow->getPlayer();
    return nullptr;
}


class VideoPlayerWidget : public QFrame, public VideoPlayer
{
	Q_OBJECT
public:
	explicit VideoPlayerWidget(QWidget* parent = 0);
	virtual ~VideoPlayerWidget();

	void pauseVideo();
	void resumeVideo();

	void stopVideo(bool showDefaultImage = false);
	bool isPaused();
    void seekByPercent(float position);

	VideoDisplay* getCurrentDisplay();
	VideoWidget* videoWidget() {return m_videoWidget;}

	void setProgressbar(VideoProgressBar* progressbar);
	void setControl(VideoControl* controlWidget);

	void setDefaultPreviewPicture();

	QString currentFilename() const;

	void updateLayout(bool fromPendingHeaderPaused = false);

	void exitFullScreen();

	void playFile(const QString& fileName);

protected:
	virtual void resizeEvent(QResizeEvent* event) override;
	virtual void wheelEvent(QWheelEvent* event) override;
	virtual bool eventFilter(QObject* object, QEvent* event) override;

public slots:
	void playPauseButtonAction();

private slots:
	void setVideoFilename(const QString& fileName);

	void updateViewOnVideoStop(bool showDefaultImage = true);

	void onPlayingFinished();

	void showSpinner();
	void hideSpinner();

signals:
	void fileReleased();

private:
    friend class VideoWidget;

	VideoControl* m_controls;
	VideoProgressBar* m_progressBar;
	QString m_currentFile;
	Spinner* m_spinner;
	VideoWidget* m_videoWidget;
};
