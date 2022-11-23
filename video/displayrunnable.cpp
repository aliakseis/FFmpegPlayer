#include "ffmpegdecoder.h"

#include <tuple>

void FFmpegDecoder::displayRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Displaying thread started";

#ifdef TRACE_DELAY_STATS
    double sumIndications = 0;
    double sqSumIndications = 0;

    enum { MAX_INDICATIONS = 200 };
    int numIndications = 0;
#endif

    while (!boost::this_thread::interruption_requested())
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
        m_frameDisplayingRequested = true;

        if (current_frame.m_convert.valid() && !current_frame.m_convert.get()) {
                finishedDisplayingFrame(m_generation);
                continue;
        }

        // Frame skip
        if (!m_videoFramesQueue.canPush()
            && m_videoStartClock + current_frame.m_pts < GetHiResTime())
        {
            CHANNEL_LOG(ffmpeg_threads) << __FUNCTION__ << " Framedrop";
            finishedDisplayingFrame(m_generation);
            continue;
        }

        // Possibly give it time to render frame
        if (m_frameListener != nullptr)
        {
            m_frameListener->updateFrame(this, m_generation);
        }

        const auto speed = getSpeedRational();

        for (;;)
        {
            const double delay = m_videoStartClock + current_frame.m_pts - GetHiResTime();
            if (delay < 0.005) {
                break;
            }
            if (delay > 0.1)
            {
                boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(100 * speed.denominator / speed.numerator));
                continue;
            }

            boost::this_thread::sleep_for(
                boost::chrono::milliseconds(int(delay * 1000. * speed.denominator / speed.numerator)));
            break;
        }

        // It's time to display converted frame
        if (current_frame.m_duration != AV_NOPTS_VALUE)
        {
            m_currentTime = current_frame.m_duration;
            if ((m_decoderListener != nullptr) && m_seekDuration == AV_NOPTS_VALUE)
            {
                m_decoderListener->changedFramePosition(
                    m_startTime,
                    current_frame.m_duration,
                    m_duration + m_startTime);
            }
        }

#ifdef TRACE_DELAY_STATS
        const double delay = m_videoStartClock + current_frame.m_pts - GetHiResTime();
        sumIndications += delay;
        sqSumIndications += delay * delay;
        if (++numIndications >= MAX_INDICATIONS)
        {
            const double avg = sumIndications / numIndications;
            CHANNEL_LOG(ffmpeg_threads) << "Average frame delay: " << avg
                << "; frame delay deviation: " << sqrt(sqSumIndications / numIndications - avg * avg);
            sumIndications = 0;
            sqSumIndications = 0;
            numIndications = 0;
        }
#endif

        if (m_frameListener != nullptr)
        {
            m_frameListener->drawFrame(this, m_generation);
        }
        else
        {
            finishedDisplayingFrame(m_generation);
        }
    }
}
