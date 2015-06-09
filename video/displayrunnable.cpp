#include "displayrunnable.h"

void DisplayRunnable::operator()()
{
    CHANNEL_LOG(ffmpeg_threads) << "Displaying thread started";

    for (;;)
    {
        {
            boost::unique_lock<boost::mutex> locker(m_ffmpeg->m_videoFramesMutex);
            m_ffmpeg->m_videoFramesCV.wait(locker, [this]()
                                     {
                                         return !m_ffmpeg->m_frameDisplayingRequested &&
                                                m_ffmpeg->m_videoFramesQueue.m_busy != 0;
                                     });
        }

        VideoFrame* current_frame =
            &m_ffmpeg->m_videoFramesQueue.m_frames[m_ffmpeg->m_videoFramesQueue.m_read_counter];

        // Frame skip
        if (m_ffmpeg->m_videoFramesQueue.m_busy > 1 && current_frame->m_displayTime < GetHiResTime())
        {
            CHANNEL_LOG(ffmpeg_threads) << __FUNCTION__ << " Framedrop";
            m_ffmpeg->finishedDisplayingFrame();
            continue;
        }

        m_ffmpeg->m_frameDisplayingRequested = true;

        // Possibly give it time to render frame
        if (m_ffmpeg->m_frameListener)
        {
            m_ffmpeg->m_frameListener->updateFrame();
        }

        for (;;)
        {
            double current_time = GetHiResTime();
            double delay = current_frame->m_displayTime - current_time;
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
        if (m_ffmpeg->m_decoderListener && current_frame->m_duration != AV_NOPTS_VALUE)
            m_ffmpeg->m_decoderListener->changedFramePosition(current_frame->m_duration, m_ffmpeg->m_duration);

        if (m_ffmpeg->m_frameListener)
        {
            m_ffmpeg->m_frameListener->drawFrame();
        }
        else
        {
            m_ffmpeg->finishedDisplayingFrame();
        }
    }
}
