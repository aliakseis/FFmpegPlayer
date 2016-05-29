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
        if (!m_videoFramesQueue.canPush() && current_frame.m_displayTime < GetHiResTime())
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
            double current_time = GetHiResTime();
            double delay = current_frame.m_displayTime - current_time;
            if (delay < 0.005)
                break;
            if (delay > 3.)
            {
                boost::this_thread::sleep_for(boost::chrono::milliseconds(3000));
                continue;
            }

            boost::this_thread::sleep_for(boost::chrono::milliseconds(int(delay * 1000.)));
            break;
        }

        // It's time to display converted frame
        if (m_decoderListener && current_frame.m_duration != AV_NOPTS_VALUE &&
            m_seekDuration == -1)
        {
            m_decoderListener->changedFramePosition(
                m_startTime,
                current_frame.m_duration,
                m_duration + m_startTime);
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
