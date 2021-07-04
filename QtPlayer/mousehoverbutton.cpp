#include "mousehoverbutton.h"
#include <QMouseEvent>

MouseHoverButton::MouseHoverButton(QWidget* parent) : QToolButton(parent)
{
}


MouseHoverButton::~MouseHoverButton()
= default;

void MouseHoverButton::mousePressEvent(QMouseEvent* event)
{
	if (m_defIcon.isNull())
	{
		m_defIcon = icon();
		QList<QSize> normalOnSizes = m_defIcon.availableSizes(QIcon::Normal, QIcon::On);
		if (!normalOnSizes.empty())
		{
			// use normal/on image as pushed one
			m_pushedIcon = m_defIcon.pixmap(*normalOnSizes.begin(), QIcon::Normal, QIcon::On);
		}
	}
	QToolButton::mousePressEvent(event);
	if (event->buttons() & Qt::LeftButton)
	{
		setIcon(m_pushedIcon);
	}
}

void MouseHoverButton::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		setIcon(m_defIcon);
	}
	QToolButton::mouseReleaseEvent(event);
}

void MouseHoverButton::keyReleaseEvent(QKeyEvent* e)
{
	Q_UNUSED(e)
	// do nothing so button won't process key events
}

void MouseHoverButton::paintEvent(QPaintEvent* event)
{
	bool isButtonDown = isDown();
	setDown(false);
	QToolButton::paintEvent(event);
	setDown(isButtonDown);
}
