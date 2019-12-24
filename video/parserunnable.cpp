#include "ffmpegdecoder.h"
#include "makeguard.h"

#include <algorithm>
#include <memory>

namespace {

bool isSeekable(AVFormatContext* formatContext)
{
    return
#ifdef AVFMTCTX_UNSEEKABLE
        ((formatContext->ctx_flags & AVFMTCTX_UNSEEKABLE) == 0) &&
#endif
        (formatContext->pb != nullptr) && ((formatContext->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0);
}

} // namespace

void FFmpegDecoder::parseRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Parse thread started";
    AVPacket packet;
    enum { UNSET, SET, REPORTED } eof = UNSET;

    // detect real framesize
    fixDuration();

    if (m_decoderListener != nullptr)
    {
        m_decoderListener->fileLoaded(m_startTime, m_duration + m_startTime);
        m_decoderListener->changedFramePosition(m_startTime, m_startTime, m_duration + m_startTime);
    }

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
            resetDecoding(seekDuration, false);
        }
        seekDuration = m_videoResetDuration.exchange(AV_NOPTS_VALUE);
        if (seekDuration != AV_NOPTS_VALUE)
        {
            if (!resetDecoding(seekDuration, true)) {
                return;
            }
        }

        const int readStatus = av_read_frame(m_formatContext, &packet);
        if (readStatus >= 0)
        {
            dispatchPacket(packet);
            eof = UNSET;
        }
        else
        {
            using namespace boost;
            if (eof == SET)
            {
                if ((m_decoderListener != nullptr)
                    && m_videoPacketsQueue.empty()
                    && m_audioPacketsQueue.empty()
                    && (lock_guard<mutex>(m_videoFramesMutex), !m_videoFramesQueue.canPop()))
                {
                    m_decoderListener->onEndOfStream();
                    eof = REPORTED;
                }
            }
            if (eof == UNSET && (readStatus == AVERROR_EOF || readStatus == AVERROR_INVALIDDATA)) {
                eof = SET;
            }

            this_thread::sleep_for(chrono::milliseconds(1));
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
    else if (std::find(m_audioIndices.begin(), m_audioIndices.end(), packet.stream_index) 
        != m_audioIndices.end())
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
        m_mainAudioThread = std::make_unique<boost::thread>(&FFmpegDecoder::audioParseRunnable, this);
    }
}

void FFmpegDecoder::startVideoThread()
{
    if (m_videoStreamNumber >= 0)
    {
        m_mainVideoThread = std::make_unique<boost::thread>(&FFmpegDecoder::videoParseRunnable, this);
    }
}

bool FFmpegDecoder::resetDecoding(int64_t seekDuration, bool resetVideo)
{
    CHANNEL_LOG(ffmpeg_seek) << __FUNCTION__ << " resetVideo=" << resetVideo;

    const bool hasVideo = m_mainVideoThread != nullptr;

    if (isSeekable(m_formatContext)
        && avformat_seek_file(m_formatContext, 
                           hasVideo ? m_videoStreamNumber : m_audioStreamNumber,
                           0, seekDuration, seekDuration, AVSEEK_FLAG_FRAME) < 0
        && (seekDuration >= 0 || avformat_seek_file(m_formatContext, 
                           hasVideo ? m_videoStreamNumber : m_audioStreamNumber,
                           0, 0, 0, AVSEEK_FLAG_FRAME) < 0))
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
    {
        boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
        m_videoFramesQueue.clear();
        m_frameDisplayingRequested = false;
        ++m_generation;
    }

    m_videoResetting = false;

    m_videoStartClock = VIDEO_START_CLOCK_NOT_INITIALIZED;

    if (resetVideo && !resetVideoProcessing())
    {
        return false;
    }

    m_mainDisplayThread = std::make_unique<boost::thread>(&FFmpegDecoder::displayRunnable, this);

    if (hasVideo)
    {
        if (m_videoCodecContext != nullptr) {
            avcodec_flush_buffers(m_videoCodecContext);
        }
    }
    if (hasAudio)
    {
        if (m_audioCodecContext != nullptr) {
            avcodec_flush_buffers(m_audioCodecContext);
        }
        m_audioPlayer->WaveOutReset();
    }

    if (m_decoderListener != nullptr)
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
    if (m_duration <= 0)
    {
        m_duration = 0;
        if (!isSeekable(m_formatContext))
        {
            return;
        }

        AVPacket packet;
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
