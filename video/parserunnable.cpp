#include "ffmpegdecoder.h"
#include "makeguard.h"

void FFmpegDecoder::parseRunnable()
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

        int64_t seekDuration = m_seekDuration.exchange(AV_NOPTS_VALUE);
        if (seekDuration != AV_NOPTS_VALUE)
        {
            if (!resetDecoding(seekDuration, false))
                return;
        }
        seekDuration = m_videoResetDuration.exchange(AV_NOPTS_VALUE);
        if (seekDuration != AV_NOPTS_VALUE)
        {
            if (!resetDecoding(seekDuration, true))
                return;
        }

        const int readStatus = av_read_frame(m_formatContext, &packet);
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
                if (m_decoderListener
                    && m_videoPacketsQueue.empty()
                    && m_audioPacketsQueue.empty()
                    && (lock_guard<mutex>(m_videoFramesMutex), !m_videoFramesQueue.canPop()))
                {
                    m_decoderListener->onEndOfStream();
                }

                this_thread::sleep_for(chrono::milliseconds(10));
            }
            eof = readStatus == AVERROR_EOF;
        }

        // Continue packet reading
    }

    CHANNEL_LOG(ffmpeg_threads) << "Decoding ended";
}

void FFmpegDecoder::dispatchPacket(AVPacket& packet)
{
    auto guard = MakeGuard(&packet, av_packet_unref);

    auto seekLambda = [this]
    {
        return m_seekDuration != AV_NOPTS_VALUE || m_videoResetDuration != AV_NOPTS_VALUE;
    };

    if (seekLambda())
    {
        return; // guard frees packet
    }

    if (packet.stream_index == m_videoStreamNumber)
    { 
        if (!m_videoPacketsQueue.push(packet, seekLambda))
        {
            return; // guard frees packet
        }
    }
    else if (packet.stream_index == m_audioStreamNumber)
    { 
        if (!m_audioPacketsQueue.push(packet, seekLambda))
        {
            return; // guard frees packet
        }
    }
    else
    {
        //auto codec = m_formatContext->streams[packet.stream_index]->codec;
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

void FFmpegDecoder::startAudioThread()
{
    if (m_audioStreamNumber >= 0)
    {
        m_mainAudioThread.reset(new boost::thread(&FFmpegDecoder::audioParseRunnable, this));
    }
}

void FFmpegDecoder::startVideoThread()
{
    if (m_videoStreamNumber >= 0)
    {
        m_mainVideoThread.reset(new boost::thread(&FFmpegDecoder::videoParseRunnable, this));
    }
}

bool FFmpegDecoder::resetDecoding(int64_t seekDuration, bool resetVideo)
{
    const bool hasVideo = m_mainVideoThread != nullptr;

    if (avformat_seek_file(m_formatContext, 
                           hasVideo ? m_videoStreamNumber : m_audioStreamNumber,
                           0, seekDuration, seekDuration, AVSEEK_FLAG_FRAME) < 0)
    {
        CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
        return false;
    }

    const bool hasAudio = m_mainAudioThread != nullptr;

    if (hasVideo)
    {
        m_mainVideoThread->interrupt();
    }
    if (hasAudio)
    {
        m_mainAudioThread->interrupt();
    }

    if (hasVideo)
    {
        m_mainVideoThread->join();
    }
    if (hasAudio)
    {
        m_mainAudioThread->join();
    }

    // Reset stuff
    m_videoPacketsQueue.clear();
    m_audioPacketsQueue.clear();

    m_mainDisplayThread->interrupt();
    m_mainDisplayThread->join();

    // Free videoFrames
    m_videoFramesQueue.clear();

    m_videoResetting = false;
    m_frameDisplayingRequested = false;

    if (resetVideo && !resetVideoProcessing())
        return false;

    m_mainDisplayThread.reset(new boost::thread(&FFmpegDecoder::displayRunnable, this));

    const auto currentTime = GetHiResTime();
    if (hasVideo)
    {
        if (m_videoCodecContext)
            avcodec_flush_buffers(m_videoCodecContext);
    }
    if (hasAudio)
    {
        if (m_audioCodecContext)
            avcodec_flush_buffers(m_audioCodecContext);
        m_audioPlayer->WaveOutReset();
    }

    if (m_decoderListener)
    {
        m_decoderListener->changedFramePosition(m_startTime, seekDuration, m_duration + m_startTime);
    }

    seekWhilePaused();

    // Restart
    startAudioThread();
    startVideoThread();

    return true;
}

void FFmpegDecoder::fixDuration()
{
    AVPacket packet;
    if (m_duration <= 0)
    {
        m_duration = 0;
        while (av_read_frame(m_formatContext, &packet) >= 0)
        {
            if (packet.stream_index == m_videoStreamNumber)
            {
                if (packet.pts != AV_NOPTS_VALUE)
                {
                    m_duration = packet.pts;
                }
                else if (packet.dts != AV_NOPTS_VALUE)
                {
                    m_duration = packet.dts;
                }
            }
            av_packet_unref(&packet);

            if (boost::this_thread::interruption_requested())
            {
                CHANNEL_LOG(ffmpeg_threads) << "Parse thread broken";
                return;
            }
        }

        if (avformat_seek_file(m_formatContext, m_videoStreamNumber, 0, 0, 0,
                               AVSEEK_FLAG_FRAME) < 0)
        {
            CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
            return;
        }
    }
}
