#pragma once

struct VideoFrame
{
    double m_displayTime;
    int64_t m_duration;
    AVFrame* m_image;

    VideoFrame() 
        : m_displayTime(0)
        , m_duration(0)
        , m_image(av_frame_alloc()) 
    {}
    ~VideoFrame()
    {
        av_frame_free(&m_image);
    }

    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;

    void free()
    {
        av_frame_unref(m_image);
    }
    void realloc(AVPixelFormat pix_fmt, int width, int height)
    {
        if (pix_fmt != m_image->format || width != m_image->width || height != m_image->height)
        {
            free();
            m_image->format = pix_fmt;
            m_image->width = width;
            m_image->height = height;
            av_frame_get_buffer(m_image, 16);
        }
    }
};
