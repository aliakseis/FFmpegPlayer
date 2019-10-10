#include "ffmpegdecoder.h"
#include "makeguard.h"
#include "interlockedadd.h"

#include <boost/log/trivial.hpp>
#include <tuple>

namespace {

bool frameToImage(
    VideoFrame& videoFrameData, 
    AVFramePtr& m_videoFrame,
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

struct FFmpegDecoder::VideoParseContext
{
    bool initialized = false;
    int numSkipped = 0;
};

void FFmpegDecoder::videoParseRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Video thread started";
    m_videoStartClock = VIDEO_START_CLOCK_NOT_INITIALIZED;
    double videoClock = 0; // pts of last decoded frame / predicted pts of next decoded frame

    VideoParseContext context{};

    for (;;)
    {
        if (m_isPaused && !m_isVideoSeekingWhilePaused)
        {
            boost::unique_lock<boost::mutex> locker(m_isPausedMutex);
            while (m_isPaused && !m_isVideoSeekingWhilePaused)
            {
                m_isPausedCV.wait(locker);
            }
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

            if (!handleVideoPacket(packet, videoClock, context)
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
    VideoParseContext& context)
{
    const int ret = avcodec_send_packet(m_videoCodecContext, &packet);
    if (ret < 0)
        return false;

    AVFramePtr videoFrame(av_frame_alloc());
    while (avcodec_receive_frame(m_videoCodecContext, videoFrame.get()) == 0)
    {
		const int64_t duration_stamp = videoFrame->best_effort_timestamp;

        // compute the exact PTS for the picture if it is omitted in the stream
        // pts1 is the dts of the pkt / pts of the frame
        if (duration_stamp != AV_NOPTS_VALUE)
        {
            videoClock = duration_stamp * av_q2d(m_videoStream->time_base);
        }
        const double pts = videoClock;

        // update video clock for next frame
        // for MPEG2, the frame can be repeated, so we update the clock accordingly
        const double frameDelay = av_q2d(m_videoCodecContext->time_base) *
            (1. + videoFrame->repeat_pict * 0.5);
        videoClock += frameDelay;

        if (!handleVideoFrame(videoFrame, pts, context))
            break;
    }

    return true;
}

bool FFmpegDecoder::handleVideoFrame(
    AVFramePtr& videoFrame,
    double pts,
    VideoParseContext& context)
{
    enum { MAX_SKIPPED = 4 };
    const double MAX_DELAY = 0.2;

    const int64_t duration_stamp = videoFrame->best_effort_timestamp;

    boost::posix_time::time_duration td(boost::posix_time::pos_infin);
    bool inNextFrame = false;
    const bool haveVideoPackets = !m_videoPacketsQueue.empty();

    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);

        inNextFrame = m_isPaused && m_isVideoSeekingWhilePaused;
        if (!context.initialized || inNextFrame)
        {
            m_videoStartClock = (m_isPaused ? m_pauseTimer : GetHiResTime()) - pts;
        }

        // Skipping frames
        if (context.initialized && !inNextFrame && haveVideoPackets)
        {
            const double curTime = GetHiResTime();
            if (m_videoStartClock + pts <= curTime)
            {
                if (m_videoStartClock + pts < curTime - MAX_DELAY)
                {
                    InterlockedAdd(m_videoStartClock, MAX_DELAY);
                }

                if (++context.numSkipped > MAX_SKIPPED)
                {
                    context.numSkipped = 0;
                }
                else
                {
                    CHANNEL_LOG(ffmpeg_sync) << "Hard skip frame";

                    // pause
                    if (m_isPaused && !m_isVideoSeekingWhilePaused)
                    {
                        return false;
                    }

                    return true;
                }
            }
            else
            {
                int speedNumerator, speedDenominator;
                std::tie(speedNumerator, speedDenominator) = getSpeedRational();
                context.numSkipped = 0;
                td = boost::posix_time::milliseconds(
                    int((m_videoStartClock + pts - curTime) * 1000.  * speedDenominator / speedNumerator) + 1);
            }
        }
    }

    context.initialized = true;

    {
        boost::unique_lock<boost::mutex> locker(m_videoFramesMutex);

        if (!m_videoFramesCV.timed_wait(locker, td, [this]
        {
            return m_isPaused && !m_isVideoSeekingWhilePaused ||
                m_videoFramesQueue.canPush();
        }))
        {
            CHANNEL_LOG(ffmpeg_sync) << "Frame wait abandoned";
            return true;
        }
    }

    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
        if (m_isPaused && !m_isVideoSeekingWhilePaused)
        {
            return false;
        }

        m_isVideoSeekingWhilePaused = false;
    }

    if (inNextFrame)
    {
        m_isPausedCV.notify_all();
    }

    VideoFrame& current_frame = m_videoFramesQueue.back();
    handleDirect3dData(videoFrame.get());
    if (!frameToImage(current_frame, videoFrame, m_imageCovertContext, m_pixelFormat))
    {
        return true;
    }

    current_frame.m_pts = pts;
    current_frame.m_duration = duration_stamp;

    {
        boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
        m_videoFramesQueue.pushBack();
    }
    m_videoFramesCV.notify_all();

    return true;
}
