#include "ffmpegdecoder.h"
#include "makeguard.h"

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

            handleVideoPacket(packet, videoClock, initialized);

            if (m_isPaused && !m_isVideoSeekingWhilePaused)
            {
                break;
            }
        }
    }
}

void FFmpegDecoder::handleVideoPacket(
    AVPacket packet,  // uses copy
    double& videoClock,
    bool& initialized)
{
    while (packet.size > 0)
    {
        int frameFinished = 0;
        const int length = avcodec_decode_video2(m_videoCodecContext,
            m_videoFrame, &frameFinished, &packet);

        if (length < 0)
        {
            // Broken packet
            break;  // Simply skip frame without errors
        }
        packet.size -= length;
        packet.data += length;

        if (!frameFinished)
        {
            continue;
        }

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
        if (!frameToImage(current_frame))
        {
            continue;
        }

        current_frame.m_displayTime = m_videoStartClock + pts;
        current_frame.m_duration = duration_stamp;

        {
            boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
            m_videoFramesQueue.pushBack();
        }
        m_videoFramesCV.notify_all();
    }
}
