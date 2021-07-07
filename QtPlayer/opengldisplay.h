#ifndef OPENGLDISPLAY_H
#define OPENGLDISPLAY_H

// https://github.com/MasterAler/SampleYUVRenderer

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QScopedPointer>
#include <QException>

#include "videodisplay.h"

class OpenGLDisplay : public QOpenGLWidget, public QOpenGLFunctions, public VideoDisplay
{
    Q_OBJECT
public:
    explicit OpenGLDisplay(QWidget* parent = nullptr);
    ~OpenGLDisplay();

    void InitDrawBuffer(unsigned bsize);
    //void DisplayVideoFrame(unsigned char *data, int frameWidth, int frameHeight);


    void displayFrame(unsigned int generation);

    void showPicture(const QImage& picture) override;
    void showPicture(const QPixmap& picture) override;

    void updateFrame(IFrameDecoder* decoder) override;
    void drawFrame(IFrameDecoder* decoder, unsigned int generation) override; // decoder->finishedDisplayingFrame() must be called
    void decoderClosing() override;

    float aspectRatio() const { return m_aspectRatio; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    int getWidth();
    int getHeight();

protected slots:
    virtual void currentDisplay(unsigned int generation);
signals:
    void display(unsigned int generation);

private:
    struct OpenGLDisplayImpl;
    QScopedPointer<OpenGLDisplayImpl> impl;

    float m_aspectRatio { 0.75f };
};

/***********************************************************************/

class OpenGlException: public QException
{
public:
     void raise() const { throw *this; }
     OpenGlException *clone() const { return new OpenGlException(*this); }
};

#endif // OPENGLDISPLAY_H
