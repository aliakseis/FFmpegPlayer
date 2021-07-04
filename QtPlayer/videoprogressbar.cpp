#include "videoprogressbar.h"
#include "ffmpegdecoder.h"
#include "videoplayerwidget.h"
#include <QPainter>
#include <QEvent>
#include <QMouseEvent>
#include <QToolTip>

#include <qdrawutil.h>

VideoProgressBar::VideoProgressBar(QWidget* parent) :
	QProgressBar(parent),
	m_scale(1000),
	m_btn_down(false),
	m_seekDisabled(false),
	m_downloadedTotalOriginal(0)
{
	m_downloaded = 0;
	m_played = 0;

	setWindowTitle("Video Progress Bar");
	resize(500, 200);
	installEventFilter(this);
}

VideoProgressBar::~VideoProgressBar()
= default;

void VideoProgressBar::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event)
	QPainter painter(this);
	QLinearGradient gradient(0, 0, 0, height());

	int lineheight = height() / 3;
	int margintop = (height() - lineheight) / 2;

	// Draw background line
	gradient.setColorAt(0, QColor(67, 67, 67));
	gradient.setColorAt(1, QColor(49, 49, 50));
	painter.setBrush(gradient);
	painter.setPen(Qt::NoPen);
	painter.drawRect(0, margintop, width(), lineheight);

	// Downloaded line
	gradient.setColorAt(0, QColor(235, 235, 235));
	gradient.setColorAt(1, QColor(207, 213, 217));
	painter.setBrush(gradient);
	painter.drawRect(0, margintop, ((double)m_downloaded / m_scale) * width(), lineheight);

	// 3 step : playing line
	gradient.setColorAt(0, QColor(209, 63, 70));
	gradient.setColorAt(1, QColor(177, 10, 11));
	painter.setBrush(gradient);
	painter.drawRect(0, margintop, ((double)m_played / m_scale) * width(), lineheight);

	QPixmap clicker = QPixmap(":/images/video_seek_cursor.png");
	painter.drawPixmap(
		QRect((m_played / (m_scale - (double)clicker.width()*m_scale / (width()))) * width() - 1 - (double)clicker.width()*m_played / m_scale * 1.8 , 1, clicker.width(), clicker.height()),
		clicker,
		clicker.rect()
	);
}

void VideoProgressBar::setDownloadedCounter(int downloaded)
{
	if (downloaded > m_scale)
	{
		downloaded = m_scale;
	}
	else if (downloaded < 0)
	{
		downloaded = 0;
	}

	if (m_downloaded == downloaded)
	{
		return;
	}

	m_downloaded = downloaded;

	repaint();
}

void VideoProgressBar::setPlayedCounter(int played)
{
	if (played < 0)
	{
		played = 0;
	}

	if (m_played == played)
	{
		return;
	}

	m_played = played;

	repaint();
}

int VideoProgressBar::getScale() const
{
	return m_scale;
}

void VideoProgressBar::resetProgress()
{
	m_downloaded = 0;
	m_played = 0;
	repaint();

	setToolTip(QString());
	if (underMouse())
	{
		QToolTip::hideText();
	}
}

bool VideoProgressBar::eventFilter(QObject* obj, QEvent* event)
{
    //const FFmpegDecoder* decoder = VideoPlayerWidgetInstance()->getDecoder();
	QEvent::Type eventType = event->type();
	switch (eventType)
	{
	case QEvent::MouseMove:
	{
		auto* mevent = static_cast<QMouseEvent*>(event);
		float percent = (mevent->x() * 1.0) / width();

		if (m_btn_down)
		{
			if (percent > 1.0)
			{
				percent = 1.0;
			}
			if (percent < 0)
			{
				percent = 0;
			}

			if (!m_seekDisabled)
			{
                VideoPlayerWidgetInstance()->seekByPercent(percent);
			}
		}
	}
	break;
	case QEvent::MouseButtonPress:
	{
		m_btn_down = true;
	}
	break;
	case QEvent::MouseButtonRelease:
	{
		auto* mevent = static_cast<QMouseEvent*>(event);
		float percent = (mevent->x() * 1.0) / width();

		if (percent > 1.0)
		{
			percent = 1.0;
		}
		if (percent < 0)
		{
			percent = 0;
		}

		if (!m_seekDisabled)
		{
            VideoPlayerWidgetInstance()->seekByPercent(percent);
		}
		m_btn_down = false;
	}
	break;
	default:;
	}

	return QWidget::eventFilter(obj, event);
}


void VideoProgressBar::displayPlayedProgress(qint64 frame, qint64 total)
{
	int progress = ((double)frame / total) * getScale();
	if (!m_seekDisabled)
	{
		setPlayedCounter(progress);
	}
	else
	{
		setPlayedCounter(0);
	}
}

void VideoProgressBar::seekingEnable(bool enable /* = true*/)
{
	m_seekDisabled = !enable;
}
