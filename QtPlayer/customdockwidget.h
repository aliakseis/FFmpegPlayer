#pragma once

#include <QWidget>

class VideoDisplay;
class VideoWidget;


class CustomDockWidget : public QWidget
{
	Q_OBJECT
public:
	CustomDockWidget(QWidget* widget = nullptr);
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
	void closeEvent(QCloseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

signals:
	bool enterFullscreen(bool f);

public slots:
	void onLeaveFullScreen();

private:
	VisibilityState m_state{ShownDocked};
	VisibilityState m_prevState{ShownDocked};
    VideoWidget* m_display{};
};
