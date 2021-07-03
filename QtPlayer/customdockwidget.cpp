#include "customdockwidget.h"
#include "mainwindow.h"
#include "videoplayerwidget.h"
#include "videowidget.h"

#include <QCloseEvent>
#include <QWidget>
#include <QEvent>

CustomDockWidget::CustomDockWidget(QWidget* widget) : QWidget(widget), m_state(ShownDocked), m_prevState(ShownDocked)
{
	Q_ASSERT(parent()->metaObject()->className() == QString("MainWindow"));
	m_parent = (MainWindow*)parent();
}

void CustomDockWidget::setDisplayForFullscreen(VideoDisplay* display)
{
	Q_ASSERT(display != nullptr);
    m_display = dynamic_cast<VideoWidget*>(display);
}

void CustomDockWidget::closeEvent(QCloseEvent* event)
{
	event->ignore();
}

void CustomDockWidget::keyPressEvent(QKeyEvent* event)
{
	// Player fullscreen in
	if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && (event->modifiers() & Qt::AltModifier))
	{
		setVisibilityState(FullScreen);
		event->ignore();
	}
    QWidget::keyPressEvent(event);
}

void CustomDockWidget::setVisibilityState(VisibilityState state)
{
	if (state == m_state)
	{
		return;
	}

	switch (state)
	{
	case ShownDocked:
	{
		this->setVisible(true);
	}
	break;
	case FullScreen:
	{
		bool prevIsFullScreen = m_display->isFullScreen();
		m_display->fullScreen(!prevIsFullScreen);
		if (m_display->isFullScreen() == prevIsFullScreen)
		{
			return;
		}
		// emit enterFullscreen(true);
	}
	break;
	}
	m_prevState = m_state;
	m_state = state;
}

void CustomDockWidget::onLeaveFullScreen()
{
	if (m_display->isFullScreen())
	{
		m_display->fullScreen(false);
	}
	setVisibilityState(m_prevState);
}
