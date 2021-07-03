#pragma once

#include <QProgressBar>

class VideoProgressBar : public QProgressBar
{
	Q_OBJECT
public:
	explicit VideoProgressBar(QWidget* parent = 0);

	virtual ~VideoProgressBar();
	int getScale() const;
	void resetProgress();

protected:
	virtual void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* obj, QEvent* event) override;

private:
	int m_downloaded;
	int m_played;
	int m_scale;
	bool m_btn_down;
	bool m_seekDisabled;
	qint64 m_downloadedTotalOriginal;
public slots:
	void setDownloadedCounter(int downloaded);
	void setPlayedCounter(int played);
	void seekingEnable(bool enable = true);
public slots:
	void displayPlayedProgress(qint64 frame, qint64 total);
};
