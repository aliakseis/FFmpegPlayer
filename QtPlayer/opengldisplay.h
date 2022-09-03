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
    ~OpenGLDisplay() override;

    void InitDrawBuffer(unsigned bsize);

    void showPicture(const QImage& img) override;
    void showPicture(const QPixmap& picture) override;

    void updateFrame(IFrameDecoder* decoder) override;
    void drawFrame(IFrameDecoder* decoder, unsigned int generation) override; // decoder->finishedDisplayingFrame() must be called
    void decoderClosing() override;

    float aspectRatio() const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    struct OpenGLDisplayImpl;
    QScopedPointer<OpenGLDisplayImpl> impl;
};

/***********************************************************************/

class OpenGlException: public QException
{
public:
     void raise() const override { throw *this; }
     OpenGlException *clone() const override { return new OpenGlException(*this); }
};

#endif // OPENGLDISPLAY_H
