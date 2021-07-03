#pragma once

#include <QLabel>

class Spinner : public QLabel
{
	Q_OBJECT
	Q_PROPERTY(bool spin READ isSpinning WRITE setSpinning)

public:
	explicit Spinner(QWidget* parent = 0);
	void show();
	void hide();

	bool isSpinning() const;
	void setSpinning(bool spin);

protected:
	virtual void timerEvent(QTimerEvent* event) override;

private:
	void startSpin();
	void stopSpin();

private:
	QPixmap m_pixmap;
	int m_currentRotateAngle;
	int m_timerId;
};
