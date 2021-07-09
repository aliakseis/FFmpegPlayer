#pragma once

#include <QProgressBar>

class VolumeProgressBar : public QProgressBar
{
	Q_OBJECT
public:
	VolumeProgressBar(QWidget* parent);
	~VolumeProgressBar() override;
protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QBrush m_borderBrush, m_backBrush;
	QBrush m_fillBorderBrush, m_fillBrush;
};
