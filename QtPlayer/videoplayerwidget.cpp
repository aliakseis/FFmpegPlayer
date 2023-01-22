#include "videoplayerwidget.h"
#include "videowidget.h"
#include "videocontrol.h"
#include "videoprogressbar.h"

#include <QDesktopServices>
#include <QResizeEvent>
#include <QPointer>
#include <QMessageBox>
#include <QDebug>

enum { PROGRESSBAR_VISIBLE_HEIGHT = 5 };

VideoPlayerWidget::VideoPlayerWidget(QWidget* parent) :
	QFrame(parent)
	, m_videoWidget(new VideoWidget(this))
{
    connect(getDecoder(), &FFmpegDecoderWrapper::onPlayingFinished, this, &VideoPlayerWidget::onPlayingFinished);

	setDisplay(m_videoWidget);
	m_videoWidget->installEventFilter(this);
}

void VideoPlayerWidget::setVideoFilename(const QString& fileName)
{
	m_currentFile = fileName;
}

VideoDisplay* VideoPlayerWidget::getCurrentDisplay()
{
	return VideoPlayer::getCurrentDisplay();
}

void VideoPlayerWidget::setDefaultPreviewPicture()
{
	m_videoWidget->setDefaultPreviewPicture();
}

QString VideoPlayerWidget::currentFilename() const
{
	return m_currentFile;
}

void VideoPlayerWidget::setProgressbar(VideoProgressBar* progressbar)
{
	Q_ASSERT(progressbar);
	m_progressBar = progressbar;
    connect(getDecoder(), &FFmpegDecoderWrapper::onChangedFramePosition, m_progressBar, &VideoProgressBar::displayPlayedProgress);
	progressbar->installEventFilter(this);
}

void VideoPlayerWidget::setControl(VideoControl* controlWidget)
{
	Q_ASSERT(controlWidget);
	m_controls = controlWidget;
	controlWidget->installEventFilter(this);
}

void VideoPlayerWidget::stopVideo(bool showDefaultImage)
{
    getDecoder()->close(true);
}

void VideoPlayerWidget::playFile(const QString& fileName)
{
	Q_ASSERT(!fileName.isEmpty());
	m_currentFile = fileName;
    FFmpegDecoderWrapper* decoder = getDecoder();
	if (decoder->openFile(m_currentFile))
    {
        decoder->play();
        setState(Playing);
        m_controls->showPlaybutton(false);

		if (m_videoWidget->isFullScreen())
		{
            m_videoWidget->setPreferredSize(m_videoWidget->width(), m_videoWidget->height());
		}
		else
		{
            updateLayout();
		}
	}
	else
	{
		m_progressBar->setDownloadedCounter(0);
		setState(InitialState);
		m_controls->showPlaybutton(true);
        QMessageBox::information(this, "Player", tr("File %1 cannot be played.").arg(fileName));
	}
}

void VideoPlayerWidget::pauseVideo()
{
	if (state() == Playing && getDecoder()->pauseResume())
	{
		setState(Paused);
	}
}

bool VideoPlayerWidget::isPaused()
{
	return (state() == Paused);
}

void VideoPlayerWidget::seekByPercent(float percent)
{
    getDecoder()->seekByPercent(percent);
}

void VideoPlayerWidget::playPauseButtonAction()
{
    if (state() == Paused)
    {
        m_controls->showPlaybutton(false);
        resumeVideo();
    }
	else if (state() == Playing)
	{
		m_controls->showPlaybutton(true);
		pauseVideo();
	}
}

void VideoPlayerWidget::resumeVideo()
{
	if (state() == Paused && getDecoder()->pauseResume()) // It actually resumes
	{
		setState(Playing);
	}
}

void VideoPlayerWidget::updateViewOnVideoStop(bool showDefaultImage /* = false*/)
{
	if (m_videoWidget->isFullScreen())
	{
		m_videoWidget->fullScreen(false);
	}

	m_controls->showPlaybutton();

    m_currentFile = QString();
    m_progressBar->resetProgress();
	setState(InitialState);

	emit fileReleased();
}

VideoPlayerWidget::~VideoPlayerWidget()
{
	stopVideo();
    m_videoWidget = nullptr;
}

void VideoPlayerWidget::resizeEvent(QResizeEvent* event)
{
	Q_UNUSED(event);
	updateLayout();
}

bool VideoPlayerWidget::eventFilter(QObject* object, QEvent* event)
{
	if (event->type() == QEvent::KeyRelease)
	{
		auto* ke = static_cast<QKeyEvent*>(event);
		if (ke->key() == Qt::Key_Space)
		{
			playPauseButtonAction();
		}
	}
	return QFrame::eventFilter(object, event);
}

void VideoPlayerWidget::updateLayout()
{
	int currWidth = width();
	int currHeight = height();

	const int controlsHeight = m_controls->getHeight();

    int minPlayerHeight = currHeight - controlsHeight;

	int playerWidth = currWidth;
	int yPos = 1;
    FFmpegDecoderWrapper* dec = getDecoder();
	Q_ASSERT(dec != nullptr);
    if (state() == InitialState)
	{
        double aspectRatio =
                (m_videoWidget->getPictureSize().height() > 0 && m_videoWidget->getPictureSize().width() >0)
                ? static_cast<double>(m_videoWidget->getPictureSize().height()) / m_videoWidget->getPictureSize().width()
                : 0.75;
		int height = aspectRatio * currWidth;
		// Display too big: do recalculation
		if (height > minPlayerHeight)	// TODO(Usrer): code refactoring
		{
			height = minPlayerHeight;
			playerWidth = static_cast<int>(static_cast<double>(minPlayerHeight) / aspectRatio);
		}

		m_videoWidget->setGeometry(0, yPos, playerWidth, height - PROGRESSBAR_VISIBLE_HEIGHT);
		yPos += height;

		QImage previewPic = m_videoWidget->startImageButton().scaled(playerWidth, yPos - PROGRESSBAR_VISIBLE_HEIGHT, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (m_videoWidget->startImageButton() == m_videoWidget->noPreviewImage())
		{
			m_videoWidget->showPicture(previewPic);
		}
		else
		{
			m_videoWidget->updatePlayButton();
			m_videoWidget->showPicture(m_videoWidget->drawPreview(previewPic));
		}
	}
	else if (dec->isPlaying())
	{
//#ifdef DEVELOPER_OPENGL
		QSize pictureSize = QSize(playerWidth, playerWidth);
        double aspectRatio = m_videoWidget->aspectRatio();
//#else
//		QSize pictureSize = dec->getPreferredSize(playerWidth, playerWidth).size();
//		double aspectRatio = (double)pictureSize.height() / pictureSize.width();
//#endif
		int playerHeight = pictureSize.width() * aspectRatio;
		// Display too big: do recalculation
		if (playerHeight > minPlayerHeight)
		{
			playerHeight = minPlayerHeight;
			playerWidth = static_cast<int>(static_cast<double>(minPlayerHeight) / aspectRatio);
		}

		if (m_videoWidget->isFullScreen())
		{
			m_videoWidget->setGeometry(0, 0, currWidth, currHeight);
//#ifndef DEVELOPER_OPENGL
            m_videoWidget->setPreferredSize(currWidth, currHeight);
//#endif
		}
		else
		{
			m_videoWidget->setGeometry(0, yPos, playerWidth, playerHeight - PROGRESSBAR_VISIBLE_HEIGHT);
//#ifndef DEVELOPER_OPENGL
            m_videoWidget->setPreferredSize(playerWidth, playerHeight);
//#endif
		}
		// Not required by opengl
#ifndef DEVELOPER_OPENGL
		if (state() == Playing)
		{
			m_videoWidget->setPixmap(m_videoWidget->pixmap()->scaledToHeight(m_videoWidget->height(), Qt::SmoothTransformation));
		}
        else if (state() == Paused)
		{
			m_videoWidget->showPicture(m_videoWidget->originalFrame().scaled(playerWidth, playerHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}
#endif
		yPos += playerHeight;
	}

    if (m_progressBar != nullptr)
    {
        int progressHeight = m_progressBar->height();
        m_progressBar->move(0, yPos - (progressHeight + PROGRESSBAR_VISIBLE_HEIGHT) / 2);
        m_progressBar->resize(playerWidth, progressHeight);
    }

	int controlsPos = (playerWidth - m_controls->getWidth()) / 2;
	if (controlsPos < 0)
	{
		controlsPos = 0;
	}
	m_controls->move(controlsPos, yPos);
	m_controls->resize(m_controls->getWidth(), controlsHeight);
}

void VideoPlayerWidget::exitFullScreen()
{
	onPlayingFinished();
}

void VideoPlayerWidget::onPlayingFinished()
{
	if ((m_videoWidget != nullptr) && m_videoWidget->isFullScreen())
	{
		m_videoWidget->fullScreen(false);
	}
}
