#pragma once

#include <QImage>
#include <QRegion>
#include <QTimer>

#include "videoplayerwidget.h"
#ifdef DEVELOPER_OPENGL
#include "opengldisplay.h"
#else
#include "widgetdisplay.h"
#endif

// FIXME: OpenGL full support
#ifdef DEVELOPER_OPENGL
class VideoWidget : public OpenGLDisplay
#else
class VideoWidget : public WidgetDisplay
#endif
{
	Q_OBJECT
	Q_PROPERTY(QImage m_noPreviewImg READ noPreviewImage WRITE setNoPreviewImage);
public:
	explicit VideoWidget(QWidget* parent = 0);
	virtual ~VideoWidget();

	void setDefaultPreviewPicture();
	QSize getPictureSize() const { return m_pictureSize; }
	QPixmap originalFrame() const { return m_originalFrame; }
	QImage startImageButton() const { return m_startImgButton; }

	QImage noPreviewImage() const { return m_noPreviewImg; }
	void setNoPreviewImage(const QImage& noImage) { m_noPreviewImg = noImage; }

	QPixmap drawPreview(const QImage& fromImage);
	void hidePlayButton();

	void updatePlayButton();

protected:
	virtual void keyPressEvent(QKeyEvent* event) override;

	void mousePressEvent(QMouseEvent* event);
	void mouseReleaseEvent(QMouseEvent* event);
	void mouseMoveEvent(QMouseEvent* event);
	void wheelEvent(QWheelEvent* event);
	void resizeEvent(QResizeEvent* event);

	QImage m_startImgButton;
	QImage m_noPreviewImg;
	QPixmap m_originalFrame;

private:
	bool m_playIndicator;
	QSize m_pictureSize;
	QImage m_defPlayButton, m_hoverPlayButton, m_clickedPlayButton;
	QImage* m_selImage;
	QImage m_fromImage;
	bool m_isMousePressed;
	const int m_playBtnRadius;
#ifdef Q_OS_LINUX
	bool m_resizeIndicator;
#endif

	qint64 m_lastMouseTime;

	QTimer m_cursorTimer;

	bool pointInButton(const QPoint& point);

	void showElements();
	void hideElements();

public Q_SLOTS:
	void fullScreen(bool isEnable = true);

protected Q_SLOTS:
    virtual void currentDisplay(unsigned int generation) override;

private Q_SLOTS:
	void getImageFinished(QImage image);
	void onCursorTimer();
	void fullScreenProcess();
Q_SIGNALS:
	void leaveFullScreen();
	void mouseClicked();
};
