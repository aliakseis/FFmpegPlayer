#include "videoparserunnable.h"
#include "makeguard.h"

void VideoParseRunnable::operator()()
{
    CHANNEL_LOG(ffmpeg_threads) << "Video thread started";
    m_ffmpeg->m_videoStartClock = GetHiResTime();
    double videoClock = 0; // pts of last decoded frame / predicted pts of next decoded frame

    bool initialized = false;

    for (;;)
    {
        if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused)
        {
            boost::unique_lock<boost::mutex> locker(m_ffmpeg->m_isPausedMutex);
            while (m_ffmpeg->m_isPaused)
            {
                m_ffmpeg->m_isPausedCV.wait(locker);
            }
            continue;
        }

        for (;;)
        {
            AVPacket packet;
            if (!m_ffmpeg->m_videoPacketsQueue.pop(packet,
                [this] { return m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused; }))
            {
                break;
            }

            auto packetGuard = MakeGuard(&packet, av_packet_unref);

            AVPacket copy(packet);

            while (copy.size > 0)
            {
                int frameFinished = 0;
                const int length = avcodec_decode_video2(m_ffmpeg->m_videoCodecContext, 
                    m_ffmpeg->m_videoFrame, &frameFinished, &copy);

                if (length < 0)
                {
                    // Broken packet
                    break;  // Simply skip frame without errors
                }
                copy.size -= length;
                copy.data += length;

                if (frameFinished)
                {
                    const int64_t duration_stamp =
                        av_frame_get_best_effort_timestamp(m_ffmpeg->m_videoFrame);

                    if (!initialized)
                    {
                        const double stamp =
                            (duration_stamp == AV_NOPTS_VALUE)
                            ? 0.
                            : av_q2d(m_ffmpeg->m_videoStream->time_base) * (double)duration_stamp;

                        m_ffmpeg->m_videoStartClock = GetHiResTime() - stamp;
                    }

                    double pts = static_cast<double>(duration_stamp);

                    // compute the exact PTS for the picture if it is omitted in the stream
                    // pts1 is the dts of the pkt / pts of the frame
                    if (pts == AV_NOPTS_VALUE)
                    {
                        pts = videoClock;
                    }
                    else
                    {
                        pts *= av_q2d(m_ffmpeg->m_videoStream->time_base);
                        videoClock = pts;
                    }

                    // update video clock for next frame
                    // for MPEG2, the frame can be repeated, so we update the clock accordingly
                    const double frameDelay = av_q2d(m_ffmpeg->m_videoCodecContext->time_base) *
                        (1. + m_ffmpeg->m_videoFrame->repeat_pict * 0.5);
                    videoClock += frameDelay;

                    boost::posix_time::time_duration td(boost::posix_time::pos_infin);
                    // Skipping frames
                    if (initialized)
                    {
                        double curTime = GetHiResTime();
                        if (m_ffmpeg->m_videoStartClock + pts <= curTime)
                        {
                            if (m_ffmpeg->m_videoStartClock + pts < curTime - 1.)
                            {
                                // adjust clock
                                for (double v = m_ffmpeg->m_videoStartClock;
                                    !m_ffmpeg->m_videoStartClock.compare_exchange_weak(v, v + 1.);)
                                {
                                }
                            }

                            CHANNEL_LOG(ffmpeg_sync) << "Hard skip frame";

                            // pause
                            if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused)
                            {
                                break;
                            }

                            continue;
                        }

                        td = boost::posix_time::milliseconds(
                            int((m_ffmpeg->m_videoStartClock + pts - curTime) * 1000.) + 1);
                    }

                    initialized = true;

                    {
                        boost::unique_lock<boost::mutex> locker(m_ffmpeg->m_videoFramesMutex);

                        auto cond = [this]()
                        {
                            return m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused ||
                                m_ffmpeg->m_videoFramesQueue.canPush();
                        };

                        if (td.is_pos_infinity())
                        {
                            m_ffmpeg->m_videoFramesCV.wait(locker, cond);
                        }
                        else if (!m_ffmpeg->m_videoFramesCV.timed_wait(locker, td, cond))
                        {
                            continue;
                        }
                    }

                    if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused)
                    {
                        break;
                    }

                    m_ffmpeg->m_isVideoSeekingWhilePaused = false;

                    VideoFrame& current_frame = m_ffmpeg->m_videoFramesQueue.back();
                    if (!m_ffmpeg->frameToImage(current_frame))
                    {
                        continue;
                    }

                    current_frame.m_displayTime = m_ffmpeg->m_videoStartClock + pts;
                    current_frame.m_duration = duration_stamp;

                    {
                        boost::lock_guard<boost::mutex> locker(m_ffmpeg->m_videoFramesMutex);
                        m_ffmpeg->m_videoFramesQueue.pushBack();
                    }
                    m_ffmpeg->m_videoFramesCV.notify_all();
                }

                if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused)
                {
                    break;
                }
            }
        }
    }
}
