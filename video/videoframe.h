#pragma once

#include <memory>

struct AVFrameDeleter
{
    void operator()(AVFrame *frame) const { av_frame_free(&frame); };
};

typedef std::unique_ptr<AVFrame, AVFrameDeleter> AVFramePtr;

struct VideoFrame
{
    double m_pts{0};
    int64_t m_duration{0};
    AVFramePtr m_image;

    VideoFrame() 
        : 
         m_image(av_frame_alloc()) 
    {}

    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;

    void free()
    {
        av_frame_unref(m_image.get());
    }
    void realloc(AVPixelFormat pix_fmt, int width, int height)
    {
        if (pix_fmt != m_image->format || width != m_image->width || height != m_image->height)
        {
            free();
            m_image->format = pix_fmt;
            m_image->width = width;
            m_image->height = height;
            av_frame_get_buffer(m_image.get(), 16);
        }
    }
};
