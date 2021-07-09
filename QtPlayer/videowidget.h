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
	explicit VideoWidget(QWidget* parent = nullptr);
	~VideoWidget() override;

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
	void keyPressEvent(QKeyEvent* event) override;

	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

	QImage m_startImgButton;
	QImage m_noPreviewImg;
	QPixmap m_originalFrame;

private:
	bool m_playIndicator{false};
	QSize m_pictureSize;
	QImage m_defPlayButton, m_hoverPlayButton, m_clickedPlayButton;
	QImage* m_selImage;
	QImage m_fromImage;
	bool m_isMousePressed{false};
	const int m_playBtnRadius{29};
#ifdef Q_OS_LINUX
	bool m_resizeIndicator;
#endif

	qint64 m_lastMouseTime{0};

	QTimer m_cursorTimer;

	bool pointInButton(const QPoint& point);

	void showElements();
	void hideElements();

public Q_SLOTS:
	void fullScreen(bool isEnable = true);

protected Q_SLOTS:
#ifndef DEVELOPER_OPENGL
    virtual void currentDisplay(unsigned int generation) override;
#endif

private Q_SLOTS:
	void getImageFinished(const QImage& image);
	void onCursorTimer();
	void fullScreenProcess();
Q_SIGNALS:
	void leaveFullScreen();
	void mouseClicked();
};
