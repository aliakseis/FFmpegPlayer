#include "ffmpegdecoder.h"

void FFmpegDecoder::displayRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Displaying thread started";

    for (;;)
    {
        {
            boost::unique_lock<boost::mutex> locker(m_videoFramesMutex);
            m_videoFramesCV.wait(locker, [this]()
                                     {
                                         return !m_frameDisplayingRequested &&
                                                m_videoFramesQueue.canPop();
                                     });
        }

        VideoFrame& current_frame = m_videoFramesQueue.front();

        // Frame skip
        if (!m_videoFramesQueue.canPush() 
            && m_videoStartClock + current_frame.m_pts < GetHiResTime())
        {
            CHANNEL_LOG(ffmpeg_threads) << __FUNCTION__ << " Framedrop";
            finishedDisplayingFrame();
            continue;
        }

        m_frameDisplayingRequested = true;

        // Possibly give it time to render frame
        if (m_frameListener)
        {
            m_frameListener->updateFrame();
        }

        for (;;)
        {
            const double delay = m_videoStartClock + current_frame.m_pts - GetHiResTime();
            if (delay < 0.005)
                break;
            if (delay > 0.1)
            {
                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                continue;
            }

            boost::this_thread::sleep_for(boost::chrono::milliseconds(int(delay * 1000.)));
            break;
        }

        // It's time to display converted frame
        if (current_frame.m_duration != AV_NOPTS_VALUE)
        {
            m_currentTime = current_frame.m_duration = current_frame.m_duration;
            if (m_decoderListener && m_seekDuration == AV_NOPTS_VALUE)
            {
                m_decoderListener->changedFramePosition(
                    m_startTime,
                    current_frame.m_duration,
                    m_duration + m_startTime);
            }
        }
        if (m_frameListener)
        {
            m_frameListener->drawFrame();
        }
        else
        {
            finishedDisplayingFrame();
        }
    }
}
