#include "ffmpegdecoder.h"
#include "makeguard.h"
#include "subtitles.h"

#include <algorithm>
#include <memory>

namespace {

bool isSeekable(AVFormatContext* formatContext)
{
    return
#ifdef AVFMTCTX_UNSEEKABLE
        ((formatContext->ctx_flags & AVFMTCTX_UNSEEKABLE) == 0) &&
#endif
        (formatContext->pb == nullptr || (formatContext->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0) &&
        (formatContext->iformat->name == nullptr || strcmp(formatContext->iformat->name, "sdp") != 0 ||
            formatContext->iformat->read_packet == nullptr ||
            formatContext->iformat->read_seek != nullptr || formatContext->iformat->read_seek2 != nullptr);
}

template<typename T>
bool RendezVous(
    boost::atomic_int64_t& duration,
    RendezVousData& data,
    unsigned int threshold,
    bool& encountered,
    T func)
{
    if (threshold == 1)
    {
        const auto prevDuration = duration.exchange(AV_NOPTS_VALUE);
        if (prevDuration != AV_NOPTS_VALUE)
        {
            if (!func(prevDuration))
                return false;
            encountered = true;
        }

        return true;
    }

    if (duration == AV_NOPTS_VALUE)
        return true;

    boost::mutex::scoped_lock lock(data.mtx);

    if (data.count == threshold - 1)
    {
        ++data.generation;
        data.count = 0;
        const auto prevDuration = duration.exchange(AV_NOPTS_VALUE);
        const bool result = func(prevDuration);
        lock.unlock();
        data.cond.notify_all();
        encountered = result;
        return result;
    }

    encountered = true;
    ++data.count;
    const unsigned int gen = data.generation;
    while (gen == data.generation)
        data.cond.wait(lock);

    return true;
}

} // namespace

void FFmpegDecoder::parseRunnable(int idx)
{
    CHANNEL_LOG(ffmpeg_threads) << "Parse thread started";
    AVPacket packet;
    enum { UNSET, SET_EOF, SET_INVALID, REPORTED } eof = UNSET;

    if (idx == 0)
    {
        // detect real framesize
        fixDuration();

        if (m_decoderListener != nullptr)
        {
            m_decoderListener->fileLoaded(m_startTime, m_duration + m_startTime);
            m_decoderListener->changedFramePosition(m_startTime, m_startTime, m_duration + m_startTime);
        }

        startAudioThread();
        startVideoThread();
    }

    int64_t lastTime = m_currentTime;
    enum { TO_RECOVER, RECOVERING, RECOVERED } recovering = RECOVERED;

    for (;;)
    {
        if (boost::this_thread::interruption_requested())
        {
            return;
        }

        bool restarted = false;

        RendezVous(m_seekDuration, m_seekRendezVous, m_formatContexts.size(), restarted,
            std::bind(&FFmpegDecoder::resetDecoding, this, std::placeholders::_1, false));

        if (!RendezVous(m_videoResetDuration, m_videoResetRendezVous, m_formatContexts.size(), restarted,
                std::bind(&FFmpegDecoder::resetDecoding, this, std::placeholders::_1, true))) {
            return;
        }

        if (restarted)
        {
            eof = UNSET;
            recovering = RECOVERED;
        }

        const int readStatus = av_read_frame(m_formatContexts[idx], &packet);
        if (readStatus >= 0)
        {
            const bool dispatched = dispatchPacket(idx, packet);
            eof = UNSET;

            if (recovering == TO_RECOVER && m_currentTime > lastTime)
            {
                lastTime = m_currentTime;
                recovering = RECOVERING;
            }
            else if (dispatched && recovering == RECOVERING && m_currentTime > lastTime)
            {
                recovering = RECOVERED;
            }
        }
        else if ((readStatus == AVERROR(10054) || readStatus == AVERROR_INVALIDDATA) && recovering == RECOVERED) // WSAECONNRESET TODO generic
        {
            recovering = TO_RECOVER;
            lastTime = m_currentTime;
            //seekDuration(lastTime);
            if (doSeekFrame(idx, lastTime, nullptr))
            {
                CHANNEL_LOG(ffmpeg_seek) << __FUNCTION__ << " Trying to recover from " << readStatus << "; index: " << idx;
            }
            else
            {
                CHANNEL_LOG(ffmpeg_seek) << __FUNCTION__ << " Can't recover from " << readStatus << "; index: " << idx;
            }
        }
        else
        {
            using namespace boost;
            if (eof == SET_EOF || eof == SET_INVALID)
            {
                if ((m_decoderListener != nullptr)
                    && m_videoPacketsQueue.empty()
                    && m_audioPacketsQueue.empty()
                    && (lock_guard<mutex>(m_videoFramesMutex), !m_videoFramesQueue.canPop()))
                {
                    if (eof == SET_EOF)
                    {
                        flush(idx);
                    }
                    m_decoderListener->onEndOfStream(idx, eof == SET_INVALID);
                    eof = REPORTED;
                }
            }
            if (eof == UNSET) {
                eof = (readStatus == AVERROR_EOF)? SET_EOF : SET_INVALID;
            }

            this_thread::sleep_for(chrono::milliseconds(1));
        }

        // Continue packet reading
    }

    CHANNEL_LOG(ffmpeg_threads) << "Decoding ended";
}

bool FFmpegDecoder::dispatchPacket(int idx, AVPacket& packet)
{
    auto guard = MakeGuard(&packet, av_packet_unref);

    if (m_seekDuration != AV_NOPTS_VALUE || m_videoResetDuration != AV_NOPTS_VALUE)
    {
        return false;
    }

    auto seekLambda = [this, idx]
    {
        if (m_seekDuration != AV_NOPTS_VALUE || m_videoResetDuration != AV_NOPTS_VALUE)
            return true;

        if (m_decoderListener != nullptr)
            m_decoderListener->onQueueFull(idx);

        return false;
    };

    if (idx == m_videoContextIndex && packet.stream_index == m_videoStreamNumber)
    { 
        if (m_videoPacketsQueue.push(packet, seekLambda))
        {
            guard.release();
            return true;
        }
    }
    else if (idx == m_audioContextIndex
        && std::find(m_audioIndices.begin(), m_audioIndices.end(), packet.stream_index) != m_audioIndices.end())
    { 
        if (m_audioPacketsQueue.push(packet, seekLambda))
        {
            guard.release();
            return true;
        }
    }
    else if (m_formatContexts[idx]->streams[packet.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
    {
        std::function<bool(double, double, const std::string&)> addIntervalCallback;
        {
            boost::lock_guard<boost::mutex> locker(m_addIntervalMutex);
            if (m_subtitleIdx == -1 || !m_addIntervalCallback)
                return false;

            const auto& subtitleItem = m_subtitleItems.at(m_subtitleIdx);
            if (subtitleItem.contextIdx != idx || subtitleItem.streamIdx != packet.stream_index)
                return false;

            addIntervalCallback = m_addIntervalCallback;

            if (m_subtitlesCodecContext == nullptr)
            {
                m_subtitlesCodecContext = MakeSubtitlesCodecContext(m_formatContexts[idx]->streams[packet.stream_index]->codecpar);
                if (m_subtitlesCodecContext == nullptr)
                {
                    return false;
                }
            }
        }

        std::string text = GetSubtitle(m_subtitlesCodecContext, packet);
        if (!text.empty())
        {
            if (!addIntervalCallback(packet.pts / 1000., (packet.pts + packet.duration) / 1000., text))
            {
                boost::lock_guard<boost::mutex> locker(m_addIntervalMutex);

                m_subtitleIdx = -1;
                m_addIntervalCallback = {};
                avcodec_free_context(&m_subtitlesCodecContext);
            }
        }
    }

    return false;
}

void FFmpegDecoder::flush(int idx)
{
    if (m_videoStreamNumber >= 0 && m_videoContextIndex == idx)
    {
        AVPacket packet{};
        packet.stream_index = m_videoStreamNumber;
        packet.pts = AV_NOPTS_VALUE;
        packet.dts = AV_NOPTS_VALUE;
        dispatchPacket(m_videoContextIndex, packet);
    }
    if (m_audioStreamNumber >= 0 && m_audioContextIndex == idx)
    {
        AVPacket packet{};
        packet.stream_index = m_audioStreamNumber;
        packet.pts = AV_NOPTS_VALUE;
        packet.dts = AV_NOPTS_VALUE;
        dispatchPacket(m_audioContextIndex, packet);
    }
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

    AVPacket packet{};
    auto guard = MakeGuard(&packet, av_packet_unref);

    for (int i = 0; i < m_formatContexts.size(); ++i)
    {
        if (!doSeekFrame(i, seekDuration, resetVideo ? nullptr : &packet))
            return false;
    }

    if (!respawn(seekDuration, resetVideo))
        return false;

    if (packet.data != nullptr)
    {
        guard.release();
        dispatchPacket(m_videoContextIndex, packet);
    }

    return true;
}

bool FFmpegDecoder::doSeekFrame(int idx, int64_t seekDuration, AVPacket* packet)
{
    auto formatContext = m_formatContexts[idx];
    if (!isSeekable(formatContext))
        return true;//continue;

    const bool handlingPrevFrame = m_isPaused && seekDuration == m_currentTime && packet != nullptr;

    if (handlingPrevFrame)
    {
        const auto threshold
            = 3LL * m_videoStream->r_frame_rate.den * m_videoStream->time_base.den
            / (2LL * m_videoStream->r_frame_rate.num * m_videoStream->time_base.num);
        seekDuration -= threshold;
    }

    const int64_t currentTime = m_currentTime;
    const bool backward = seekDuration < currentTime;
    const int streamNumber = (idx == m_videoContextIndex) ? m_videoStreamNumber : m_audioStreamNumber.load();

    auto convertedSeekDuration = seekDuration;
    if (idx != m_videoContextIndex && m_videoContextIndex != -1)
    {
        convertedSeekDuration = seekDuration * av_q2d(m_videoStream->time_base) / av_q2d(m_audioStream->time_base);
    }

    if (handlingPrevFrame)
    {
        if (av_seek_frame(formatContext, streamNumber, convertedSeekDuration, AVSEEK_FLAG_BACKWARD) < 0)
        {
            CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
            return false;
        }
    }
    else
    {
        if (av_seek_frame(formatContext, streamNumber, convertedSeekDuration, 0) < 0
            && ((convertedSeekDuration >= 0
                ? av_seek_frame(formatContext, streamNumber, convertedSeekDuration, AVSEEK_FLAG_BACKWARD)
                : av_seek_frame(formatContext, streamNumber, 0, 0)) < 0))
        {
            CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
            return false;
        }
    }

    if (packet && backward && idx == m_videoContextIndex && convertedSeekDuration > m_startTime)
    {
        const int readStatus = av_read_frame(formatContext, packet);
        if (readStatus >= 0)
        {
            auto pts = packet->pts;
            if (pts != AV_NOPTS_VALUE)
            {
                if (packet->stream_index != m_videoStreamNumber)
                {
                    const auto& time_base = formatContext->streams[packet->stream_index]->time_base;
                    pts = (time_base.den && time_base.num)
                        ? pts * av_q2d(time_base) / av_q2d(m_videoStream->time_base) : 0;
                }
                if (pts > currentTime)
                {
                    const int flags = handlingPrevFrame ? AVSEEK_FLAG_BACKWARD : 0;
                    av_packet_unref(packet);
                    if (av_seek_frame(formatContext, streamNumber, m_startTime, flags) < 0
                        || av_seek_frame(formatContext, streamNumber, convertedSeekDuration, flags) < 0)
                    {
                        CHANNEL_LOG(ffmpeg_seek) << "Seek correction failed";
                        return false;
                    }
                }
            }
        }
    }

    if (handlingPrevFrame)
    {
        m_prevTime = seekDuration;
        m_isVideoSeekingWhilePaused = true;
    }

    return true;
}

bool FFmpegDecoder::respawn(int64_t seekDuration, bool resetVideo)
{
    const bool hasVideo = m_mainVideoThread != nullptr;
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
        const int streamNumber =
            (m_videoContextIndex == 0) ? m_videoStreamNumber : m_audioStreamNumber.load();

        m_duration = 0;
        if (!isSeekable(m_formatContexts[0]))
        {
            return;
        }

        AVPacket packet;
        while (av_read_frame(m_formatContexts[0], &packet) >= 0)
        {
            if (packet.stream_index == streamNumber)
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

        if (avformat_seek_file(m_formatContexts[0], streamNumber, 0, 0, 0,
                               AVSEEK_FLAG_FRAME) < 0)
        {
            CHANNEL_LOG(ffmpeg_seek) << "Seek failed";
            return;
        }
    }
}
