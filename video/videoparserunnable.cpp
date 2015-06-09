#include "videoparserunnable.h"

bool VideoParseRunnable::getVideoPacket(AVPacket* packet)
{
    bool wasFull;
    {
        boost::unique_lock<boost::mutex> locker(m_ffmpeg->m_videoPacketsQueueMutex);

        while (m_ffmpeg->m_videoPacketsQueue.empty())
        {
            if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused)
            {
                return false;
            }
            m_ffmpeg->m_videoPacketsQueueCV.wait(locker);
        }

        wasFull = m_ffmpeg->isVideoPacketsQueueFull();
        *packet = m_ffmpeg->m_videoPacketsQueue.dequeue();
    }
    if (wasFull)
    {
        m_ffmpeg->m_videoPacketsQueueCV.notify_all();
    }

    return true;
}

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
            if (!getVideoPacket(&packet))
            {
                break;
            }

            int frameFinished = 0;

            auto res = avcodec_decode_video2(m_ffmpeg->m_videoCodecContext, m_ffmpeg->m_videoFrame,
                                             &frameFinished, &packet);
            av_packet_unref(&packet);

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
                            m_ffmpeg->m_videoFramesQueue.m_busy < VQueue::QUEUE_SIZE;
                    };

                    if (td.is_pos_infinity())
                    {
                        m_ffmpeg->m_videoFramesCV.wait(locker, cond);
                    }
                    else if (!m_ffmpeg->m_videoFramesCV.timed_wait(locker, td, cond))
                    {
                        continue;
                    }

                    assert(m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused ||
                           !(m_ffmpeg->m_videoFramesQueue.m_write_counter ==
                                 m_ffmpeg->m_videoFramesQueue.m_read_counter &&
                             m_ffmpeg->m_videoFramesQueue.m_busy > 0));
                }

                if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isVideoSeekingWhilePaused)
                {
                    break;
                }

                m_ffmpeg->m_isVideoSeekingWhilePaused = false;

                const int wrcount = m_ffmpeg->m_videoFramesQueue.m_write_counter;
                VideoFrame& current_frame = m_ffmpeg->m_videoFramesQueue.m_frames[wrcount];
                if (!m_ffmpeg->frameToImage(current_frame))
                {
                    continue;
                }

                current_frame.m_displayTime = m_ffmpeg->m_videoStartClock + pts;
                current_frame.m_duration = duration_stamp;

                m_ffmpeg->m_videoFramesQueue.m_write_counter =
                    (m_ffmpeg->m_videoFramesQueue.m_write_counter + 1) % VQueue::QUEUE_SIZE;

                {
                    boost::lock_guard<boost::mutex> locker(m_ffmpeg->m_videoFramesMutex);
                    ++m_ffmpeg->m_videoFramesQueue.m_busy;
                    assert(m_ffmpeg->m_videoFramesQueue.m_busy <= VQueue::QUEUE_SIZE);
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
