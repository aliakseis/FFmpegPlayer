#include "parserunnable.h"
#include "videoparserunnable.h"
#include "audioparserunnable.h"
#include "makeguard.h"

void ParseRunnable::operator()()
{
    CHANNEL_LOG(ffmpeg_threads) << "Parse thread started";
    AVPacket packet;
    bool eof = false;

    // detect real framesize
    fixDuration();

    startAudioThread();
    startVideoThread();

    for (;;)
    {
        if (boost::this_thread::interruption_requested())
        {
            return;
        }

        seek();

        const int readStatus = av_read_frame(m_ffmpeg->m_formatContext, &packet);
        if (readStatus >= 0)
        {
            dispatchPacket(packet);
            eof = false;
        }
        else
        {
            if (eof)
            {
                using namespace boost;
                if (m_ffmpeg->m_decoderListener
                    && m_ffmpeg->m_videoPacketsQueue.empty()
                    && m_ffmpeg->m_audioPacketsQueue.empty()
                    && (lock_guard<mutex>(m_ffmpeg->m_videoFramesMutex)
                        , !m_ffmpeg->m_videoFramesQueue.canPop()))
                {
                    m_ffmpeg->m_decoderListener->onEndOfStream();
                }

                this_thread::sleep_for(chrono::milliseconds(10));
            }
            eof = readStatus == AVERROR_EOF;
        }

        // Continue packet reading
    }

    CHANNEL_LOG(ffmpeg_threads) << "Decoding ended";
}

void ParseRunnable::dispatchPacket(AVPacket& packet)
{
    auto guard = MakeGuard(&packet, av_packet_unref);

    if (m_ffmpeg->m_seekDuration >= 0)
    {
        return; // guard frees packet
    }

    if (packet.stream_index == m_ffmpeg->m_videoStreamNumber)
    { 
        if (!m_ffmpeg->m_videoPacketsQueue.push(
            packet, [this] { return m_ffmpeg->m_seekDuration >= 0; }))
        {
            return; // guard frees packet
        }
    }
    else if (packet.stream_index == m_ffmpeg->m_audioStreamNumber)
    { 
        if (!m_ffmpeg->m_audioPacketsQueue.push(
            packet, [this] { return m_ffmpeg->m_seekDuration >= 0; }))
        {
            return; // guard frees packet
        }
    }
    else
    {
        //auto codec = m_ffmpeg->m_formatContext->streams[packet.stream_index]->codec;
        //if (codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
        //{
        //    AVSubtitle subtitle {};
        //    int has_subtitle = 0;
        //    avcodec_decode_subtitle2(codec, &subtitle, &has_subtitle, &packet);
        //    if (has_subtitle)
        //    {
        //        auto format = subtitle.format;
        //    }
        //}

        return; // guard frees packet
    }

    guard.release();
}

void ParseRunnable::startAudioThread()
{
    if (m_ffmpeg->m_audioStreamNumber >= 0)
    {
        m_ffmpeg->m_mainAudioThread.reset(new boost::thread(AudioParseRunnable(m_ffmpeg)));
    }
}

void ParseRunnable::startVideoThread()
{
    if (m_ffmpeg->m_videoStreamNumber >= 0)
    {
        m_ffmpeg->m_mainVideoThread.reset(new boost::thread(VideoParseRunnable(m_ffmpeg)));
    }
}

void ParseRunnable::seek()
{
    const int64_t seekDuration = m_ffmpeg->m_seekDuration.exchange(-1);
    if (seekDuration < 0)
    {
        return;
    }

    const bool hasVideo = m_ffmpeg->m_mainVideoThread != nullptr;

    if (avformat_seek_file(m_ffmpeg->m_formatContext, hasVideo ? m_ffmpeg->m_videoStreamNumber
                                                               : m_ffmpeg->m_audioStreamNumber,
                           0, seekDuration, seekDuration, AVSEEK_FLAG_FRAME) < 0)
    {
        CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
        return;
    }

    const bool hasAudio = m_ffmpeg->m_mainAudioThread != nullptr;

    if (hasVideo)
    {
        m_ffmpeg->m_mainVideoThread->interrupt();
    }
    if (hasAudio)
    {
        m_ffmpeg->m_mainAudioThread->interrupt();
    }

    if (hasVideo)
    {
        m_ffmpeg->m_mainVideoThread->join();
    }
    if (hasAudio)
    {
        m_ffmpeg->m_mainAudioThread->join();
    }

    // Reset stuff
    m_ffmpeg->m_videoPacketsQueue.clear();
    m_ffmpeg->m_audioPacketsQueue.clear();

    const auto currentTime = GetHiResTime();
    if (hasVideo)
    {
        if (m_ffmpeg->m_videoCodecContext)
            avcodec_flush_buffers(m_ffmpeg->m_videoCodecContext);
        m_ffmpeg->m_videoFramesQueue.setDisplayTime(currentTime);
    }
    if (hasAudio)
    {
        if (m_ffmpeg->m_audioCodecContext)
            avcodec_flush_buffers(m_ffmpeg->m_audioCodecContext);
        m_ffmpeg->m_audioPlayer->WaveOutReset();
    }

    m_ffmpeg->seekWhilePaused();

    // Restart
    startAudioThread();
    startVideoThread();
}

void ParseRunnable::fixDuration()
{
    AVPacket packet;
    if (m_ffmpeg->m_duration <= 0)
    {
        m_ffmpeg->m_duration = 0;
        while (av_read_frame(m_ffmpeg->m_formatContext, &packet) >= 0)
        {
            if (packet.stream_index == m_ffmpeg->m_videoStreamNumber)
            {
                if (packet.pts != AV_NOPTS_VALUE)
                {
                    m_ffmpeg->m_duration = packet.pts;
                }
                else if (packet.dts != AV_NOPTS_VALUE)
                {
                    m_ffmpeg->m_duration = packet.dts;
                }
            }
            av_packet_unref(&packet);

            if (boost::this_thread::interruption_requested())
            {
                CHANNEL_LOG(ffmpeg_threads) << "Parse thread broken";
                return;
            }
        }

        if (avformat_seek_file(m_ffmpeg->m_formatContext, m_ffmpeg->m_videoStreamNumber, 0, 0, 0,
                               AVSEEK_FLAG_FRAME) < 0)
        {
            CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
            return;
        }
    }
}
