#include "spinner.h"
#include <QPixmap>
#include <QMouseEvent>

Spinner::Spinner(QWidget* parent) :
	QLabel(parent),
	m_currentRotateAngle(0),
	m_timerId(0)
{
	setPixmap(QPixmap(QString::fromUtf8(":/busy")));
	setAlignment(Qt::AlignCenter);
	setFrameShape(QFrame::NoFrame);
	setLineWidth(0);
	setAttribute(Qt::WA_TranslucentBackground);
	setMouseTracking(true);
}

void Spinner::show()
{
	startSpin();
	QLabel::show();
}

void Spinner::hide()
{
	stopSpin();
	QLabel::hide();
}

bool Spinner::isSpinning() const
{
	return m_timerId != 0;
}

void Spinner::setSpinning(bool spin)
{
	spin ? startSpin() : stopSpin();
}

void Spinner::timerEvent(QTimerEvent* event)
{
	Q_UNUSED(event)

	if (m_pixmap.isNull())
	{
		m_pixmap = *pixmap();
	}

	const int pwidth = m_pixmap.width();
	const int pheight = m_pixmap.height();
	QTransform matrix;
	matrix.translate(pwidth / 2, pheight / 2);
	matrix.rotate(m_currentRotateAngle);
	m_currentRotateAngle += 9;
	if (m_currentRotateAngle >= 360)
	{
		m_currentRotateAngle = 0;
	}
	matrix.translate(-pwidth / 2, -pheight / 2);

	setPixmap(m_pixmap.transformed(matrix, Qt::FastTransformation));
}

void Spinner::startSpin()
{
	if (m_timerId == 0)
	{
		m_timerId = startTimer(50);
	}
}

void Spinner::stopSpin()
{
	if (m_timerId != 0)
	{
		killTimer(m_timerId);
		m_timerId = 0;
	}
}
