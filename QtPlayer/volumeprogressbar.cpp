#include "volumeprogressbar.h"

#include <QPainter>

VolumeProgressBar::VolumeProgressBar(QWidget* parent) : QProgressBar(parent),
	m_borderBrush(QColor(101, 105, 108)),
	m_backBrush(QColor(66, 66, 66)),
	m_fillBorderBrush(QColor(176, 180, 183)),
	m_fillBrush(QColor(255, 255, 255))
{
}

VolumeProgressBar::~VolumeProgressBar() = default;

void VolumeProgressBar::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event)
	QPainter painter(this);

	painter.fillRect(0, 0, width(), height(), m_borderBrush);
	painter.fillRect(1, 1, width() - 2, height() - 2, m_backBrush);

	int filledWidth = static_cast<int>(width() / 100.0 * value());

	painter.fillRect(0, 0, filledWidth, height(), m_fillBorderBrush);
	if (filledWidth > 1)
	{
		painter.fillRect(1, 1, filledWidth - 2, height() - 2, m_fillBrush);
	}
}
