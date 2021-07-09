#pragma once

#include <QToolButton>
#include <QIcon>

/*
	Since Qt tool button pushes icon's image for pressed state, we have own implementation.
	To get it properly worked next images should be assigned to
	next button states programmatically or in the Qt designer:
	Normal Off - default image
	Normal On - clicked image
	Disabled Off - disabled image
	Disabled On - disabled image
	Active Off - hover image
	Active On - hover image
	Selected Off - default image
	Selected On - default image
*/
class MouseHoverButton : public QToolButton
{
	Q_OBJECT
public:
	MouseHoverButton(QWidget* parent);
	~MouseHoverButton() override;
protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void keyReleaseEvent(QKeyEvent* e) override;
	void paintEvent(QPaintEvent* event) override;
private:
	QIcon m_defIcon;
	QIcon m_pushedIcon;
};
