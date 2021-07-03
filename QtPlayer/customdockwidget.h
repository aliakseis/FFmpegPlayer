#pragma once

#include <QWidget>
#include <QLabel>

class VideoDisplay;
class VideoWidget;
class MainWindow;


class CustomDockWidget : public QWidget
{
	Q_OBJECT
public:
	CustomDockWidget(QWidget* widget = 0);
	void setDisplayForFullscreen(VideoDisplay* display);

	enum VisibilityState
	{
		ShownDocked = 0,
		FullScreen
	};

	void setVisibilityState(VisibilityState state);
	VisibilityState currentState() const { return m_state; }
	VisibilityState previousState() const { return m_prevState; }

protected:
	virtual void closeEvent(QCloseEvent* event) override;
	virtual void keyPressEvent(QKeyEvent* event) override;

signals:
	bool enterFullscreen(bool f);

public slots:
	void onLeaveFullScreen();

private:
	VisibilityState m_state;
	VisibilityState m_prevState;
	VideoWidget* m_display;
	MainWindow* m_parent;
};
