#pragma once

#include <QWidget>
#include "videoplayerwidget.h"

namespace Ui
{
class VideoControl;
}

class VideoControl : public QWidget
{
	Q_OBJECT

public:
	explicit VideoControl(VideoPlayerWidget* parent = nullptr);
	~VideoControl() override;
	void setVolume(int volume, bool onlyWidget = false);
	void showPlaybutton(bool show = true);

	int getHeight() const { return m_height; };
	int getWidth() const;

signals:
	void download();
	void browse();

protected:
	bool eventFilter(QObject* obj, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event)override;

public slots:
	void on_btnPlay_clicked();
	void on_btnPause_clicked();

private slots:
	void onProgramVolumeChange(double volume);
	void on_btnVolume_clicked();
	void on_btnStop_clicked();
	void on_btnBrowser_clicked();

private:
	Ui::VideoControl* ui;
	VideoPlayerWidget* videoPlayer;
	int m_height;
	QPixmap background;
	QPixmap backgroundfs;
	bool m_isVolumeOn{false};
	int m_prevVolumeValue{0};

	void switchVolumeButton(bool volumeOn);
};
