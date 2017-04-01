#include "ffmpegdecoder.h"
#include "makeguard.h"

#include <boost/log/trivial.hpp>

namespace {

bool frameToImage(
    VideoFrame& videoFrameData, 
    AVFrame*& m_videoFrame,
    SwsContext*& m_imageCovertContext,
    AVPixelFormat m_pixelFormat)
{
    if (m_videoFrame->format == m_pixelFormat
        || m_videoFrame->format == AV_PIX_FMT_DXVA2_VLD)
    {
        std::swap(m_videoFrame, videoFrameData.m_image);
    }
    else
    {
        const int width = m_videoFrame->width;
        const int height = m_videoFrame->height;

        videoFrameData.realloc(m_pixelFormat, width, height);

        // Prepare image conversion
        m_imageCovertContext =
            sws_getCachedContext(m_imageCovertContext, m_videoFrame->width, m_videoFrame->height,
            (AVPixelFormat)m_videoFrame->format, width, height, m_pixelFormat,
            0, nullptr, nullptr, nullptr);

        assert(m_imageCovertContext != nullptr);

        if (m_imageCovertContext == nullptr)
        {
            return false;
        }

        // Doing conversion
        if (sws_scale(m_imageCovertContext, m_videoFrame->data, m_videoFrame->linesize, 0,
            m_videoFrame->height, videoFrameData.m_image->data, videoFrameData.m_image->linesize) <= 0)
        {
            assert(false && "sws_scale failed");
            BOOST_LOG_TRIVIAL(error) << "sws_scale failed";
            return false;
        }

        videoFrameData.m_image->sample_aspect_ratio = m_videoFrame->sample_aspect_ratio;
    }

    return true;
}


} // namespace

void FFmpegDecoder::videoParseRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Video thread started";
    m_videoStartClock = GetHiResTime();
    double videoClock = 0; // pts of last decoded frame / predicted pts of next decoded frame

    bool initialized = false;

    for (;;)
    {
        if (m_isPaused && !m_isVideoSeekingWhilePaused)
        {
            boost::unique_lock<boost::mutex> locker(m_isPausedMutex);
            while (m_isPaused)
            {
                m_isPausedCV.wait(locker);
            }
            continue;
        }

        for (;;)
        {
            AVPacket packet;
            if (!m_videoPacketsQueue.pop(packet,
                [this] { return m_isPaused && !m_isVideoSeekingWhilePaused; }))
            {
                break;
            }

            auto packetGuard = MakeGuard(&packet, av_packet_unref);

            if (!handleVideoPacket(packet, videoClock, initialized)
                || m_isPaused && !m_isVideoSeekingWhilePaused)
            {
                break;
            }
        }
    }
}

bool FFmpegDecoder::handleVideoPacket(
    const AVPacket& packet,
    double& videoClock,
    bool& initialized)
{
    const int ret = avcodec_send_packet(m_videoCodecContext, &packet);
    if (ret < 0)
        return false;

    while (avcodec_receive_frame(m_videoCodecContext, m_videoFrame) == 0)
    {
        const int64_t duration_stamp =
            av_frame_get_best_effort_timestamp(m_videoFrame);

        // compute the exact PTS for the picture if it is omitted in the stream
        // pts1 is the dts of the pkt / pts of the frame
        if (duration_stamp != AV_NOPTS_VALUE)
        {
            videoClock = duration_stamp * av_q2d(m_videoStream->time_base);
        }
        const double pts = videoClock;

        if (!initialized)
        {
            m_videoStartClock = GetHiResTime() - pts;
        }

        // update video clock for next frame
        // for MPEG2, the frame can be repeated, so we update the clock accordingly
        const double frameDelay = av_q2d(m_videoCodecContext->time_base) *
            (1. + m_videoFrame->repeat_pict * 0.5);
        videoClock += frameDelay;

        boost::posix_time::time_duration td(boost::posix_time::pos_infin);
        // Skipping frames
        if (initialized)
        {
            double curTime = GetHiResTime();
            if (m_videoStartClock + pts <= curTime)
            {
                if (m_videoStartClock + pts < curTime - 1.)
                {
                    // adjust clock
                    for (double v = m_videoStartClock;
                        !m_videoStartClock.compare_exchange_weak(v, v + 1.);)
                    {
                    }
                }

                CHANNEL_LOG(ffmpeg_sync) << "Hard skip frame";

                // pause
                if (m_isPaused && !m_isVideoSeekingWhilePaused)
                {
                    break;
                }

                continue;
            }

            td = boost::posix_time::milliseconds(
                int((m_videoStartClock + pts - curTime) * 1000.) + 1);
        }

        initialized = true;

        {
            boost::unique_lock<boost::mutex> locker(m_videoFramesMutex);

            auto cond = [this]()
            {
                return m_isPaused && !m_isVideoSeekingWhilePaused ||
                    m_videoFramesQueue.canPush();
            };

            if (td.is_pos_infinity())
            {
                m_videoFramesCV.wait(locker, cond);
            }
            else if (!m_videoFramesCV.timed_wait(locker, td, cond))
            {
                continue;
            }
        }

        if (m_isPaused && !m_isVideoSeekingWhilePaused)
        {
            break;
        }

        m_isVideoSeekingWhilePaused = false;

        VideoFrame& current_frame = m_videoFramesQueue.back();
        if (!frameToImage(current_frame, m_videoFrame, m_imageCovertContext, m_pixelFormat))
        {
            continue;
        }

        current_frame.m_pts = pts;
        current_frame.m_duration = duration_stamp;

        {
            boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
            m_videoFramesQueue.pushBack();
        }
        m_videoFramesCV.notify_all();
    }

    return true;
}
