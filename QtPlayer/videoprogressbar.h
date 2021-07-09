#pragma once

#include <QProgressBar>

class VideoProgressBar : public QProgressBar
{
	Q_OBJECT
public:
	explicit VideoProgressBar(QWidget* parent = nullptr);

	~VideoProgressBar() override;
	int getScale() const;
	void resetProgress();

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* obj, QEvent* event) override;

private:
	int m_downloaded;
	int m_played;
	int m_scale{1000};
	bool m_btn_down{false};
	bool m_seekDisabled{false};
	qint64 m_downloadedTotalOriginal{0};
public slots:
	void setDownloadedCounter(int downloaded);
	void setPlayedCounter(int played);
	void seekingEnable(bool enable = true);
public slots:
	void displayPlayedProgress(qint64 frame, qint64 total);
};
