#include "ffmpegdecoder.h"
#include "makeguard.h"
#include "interlockedadd.h"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <tuple>


namespace {

uint8_t** getAudioData(AVFrame* audioFrame)
{
    return audioFrame->extended_data != nullptr
        ? audioFrame->extended_data
        : &audioFrame->data[0];
}

int64_t getChannelLayout(AVFrame* audioFrame)
{
	const int audioFrameChannels = audioFrame->channels; //av_frame_get_channels(audioFrame);
    return ((audioFrame->channel_layout != 0u) &&
        audioFrameChannels == av_get_channel_layout_nb_channels(audioFrame->channel_layout))
        ? audioFrame->channel_layout
        : av_get_default_channel_layout(audioFrameChannels);
}

} // namespace


void FFmpegDecoder::audioParseRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Audio thread started";
    AVPacket packet;

    bool initialized = false;
    bool failed = false;

    m_audioPlayer->InitializeThread();
    auto deinitializeThread = MakeGuard(
        m_audioPlayer.get(),
        std::mem_fn(&IAudioPlayer::DeinitializeThread));

    std::vector<uint8_t> resampleBuffer;

    double scheduledEndTime = 0;

    auto useHandleAudioResultLam = [this, &failed](bool result)
    {
        if (result)
        {
            if (failed)
            {
                failed = false;
                boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
                if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
                {
                    m_audioPTS = (m_isPaused ? m_pauseTimer : GetHiResTime()) - m_videoStartClock;
                }
            }
        }
        else
        {
            failed = true;
        }
    };

    while (!boost::this_thread::interruption_requested())
    {
        if (!m_audioPacketsQueue.pop(packet))
        {
            break;
        }

        auto packetGuard = MakeGuard(&packet, av_packet_unref);

        if (m_audioStreamNumber != packet.stream_index)
        {
            continue;
        }

        if (!initialized)
        {
            if (packet.pts == AV_NOPTS_VALUE)
            {
                if (packet.data == nullptr)
                    continue;

                assert(false && "No audio pts found");
                return;
            }
            const double pts = av_q2d(m_audioStream->time_base) * packet.pts;
            m_audioPTS = pts;
            scheduledEndTime = pts;
            // invoke changedFramePosition() if needed
            //AppendFrameClock(0);
        }
        else if (packet.pts != AV_NOPTS_VALUE)
        {
            const double diff = av_q2d(m_audioStream->time_base) * packet.pts - scheduledEndTime;
            if (diff > 0.01)
            {
                CHANNEL_LOG(ffmpeg_sync) << "Patching audio frame diff: " << diff;
                if (m_formatContexts.size() == 1 && m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
                {
                    InterlockedAdd(m_videoStartClock, -diff);
                    InterlockedAdd(m_audioPTS, diff);
                }
                else
                {
                    const int size_multiplier = m_audioSettings.channels *
                        av_get_bytes_per_sample(m_audioSettings.format);

                    const int numSteps = (diff + 0.1) / 0.1;
                    const auto frame_clock = diff / numSteps;
                    for (int i = 0; i < numSteps; ++i)
                    {
                        const auto speed = getSpeedRational();
                        const int nb_samples = frame_clock * m_audioSettings.frequency * speed.denominator / speed.numerator;
                        const auto write_size = nb_samples * size_multiplier;
                        std::vector<uint8_t> buf(write_size, 0);
                        useHandleAudioResultLam(handleAudioFrame(frame_clock, buf.data(), write_size, failed));
                    }
                }
            }

            scheduledEndTime += diff;
        }

        initialized = true;

        useHandleAudioResultLam(handleAudioPacket(packet, resampleBuffer, failed, scheduledEndTime));
    }
}

bool FFmpegDecoder::handleAudioPacket(
    const AVPacket& packet,
    std::vector<uint8_t>& resampleBuffer,
    bool failed, double& scheduledEndTime)
{
    if (packet.stream_index != m_audioStream->index)
    {
        avcodec_close(m_audioCodecContext);
        m_audioStream = m_formatContexts[m_audioContextIndex]->streams[packet.stream_index];
        if (!setupAudioCodec())
        {
            return false;
        }
    }

    const int ret = avcodec_send_packet(m_audioCodecContext, &packet);
    if (ret < 0) {
        return ret == AVERROR(EAGAIN) || ret == AVERROR_EOF;
    }

    AVFramePtr audioFrame(av_frame_alloc());
    bool result = true;
    while (avcodec_receive_frame(m_audioCodecContext, audioFrame.get()) == 0)
    {
        if (audioFrame->nb_samples <= 0)
        {
            continue;
        }

        const int original_buffer_size = av_samples_get_buffer_size(
            nullptr, audioFrame->channels,
            audioFrame->nb_samples,
            static_cast<AVSampleFormat>(audioFrame->format), 1);

        // write buffer
        uint8_t* write_data = *getAudioData(audioFrame.get());
        int64_t write_size = original_buffer_size;

        setupAudioSwrContext(audioFrame.get());

        if (m_audioSwrContext != nullptr)
        {
            enum { EXTRA_SPACE = 256 };

            const int out_count = static_cast<int64_t>(audioFrame->nb_samples) *
                m_audioSettings.frequency /
                m_audioCurrentPref.frequency + EXTRA_SPACE;

            const int size_multiplier = m_audioSettings.channels *
                av_get_bytes_per_sample(m_audioSettings.format);

            const size_t buffer_size = out_count * size_multiplier;

            if (resampleBuffer.size() < buffer_size)
            {
                resampleBuffer.resize(buffer_size);
            }

            // Code for resampling
            uint8_t *out = resampleBuffer.data();
            const int converted_size = swr_convert(
                m_audioSwrContext, 
                &out,
                out_count,
                const_cast<const uint8_t**>(getAudioData(audioFrame.get())),
                audioFrame->nb_samples);

            if (converted_size < 0)
            {
                BOOST_LOG_TRIVIAL(error) << "swr_convert() failed";
                break;
            }

            if (converted_size == out_count)
            {
                BOOST_LOG_TRIVIAL(warning) << "audio buffer is probably too small";
                swr_init(m_audioSwrContext);
            }

            write_data = out;
            write_size = converted_size * size_multiplier;

            assert(write_size < buffer_size);
        }

        const double frame_clock 
            = audioFrame->sample_rate != 0? double(audioFrame->nb_samples) / audioFrame->sample_rate : 0;

        scheduledEndTime += frame_clock;

        if (!handleAudioFrame(frame_clock, write_data, write_size, failed))
        {
            result = false;
        }
    }

    return result;
}

void FFmpegDecoder::setupAudioSwrContext(AVFrame* audioFrame)
{
    const auto audioFrameFormat = static_cast<AVSampleFormat>(audioFrame->format);
    const int audioFrameChannels = audioFrame->channels;

    const int64_t dec_channel_layout = getChannelLayout(audioFrame);

    const auto speed = getSpeedRational();

    // Check if the new swr context required
    if (audioFrameFormat != m_audioCurrentPref.format ||
        dec_channel_layout != m_audioCurrentPref.channel_layout ||
        (audioFrame->sample_rate * speed.numerator) / speed.denominator != m_audioCurrentPref.frequency)
    {
        swr_free(&m_audioSwrContext);
        m_audioSwrContext = swr_alloc_set_opts(
            nullptr, m_audioSettings.channel_layout, m_audioSettings.format,
            m_audioSettings.frequency * speed.denominator,
            dec_channel_layout, audioFrameFormat,
            audioFrame->sample_rate * speed.numerator, 0, nullptr);

        if ((m_audioSwrContext == nullptr) || swr_init(m_audioSwrContext) < 0)
        {
            BOOST_LOG_TRIVIAL(error) << "unable to initialize swr convert context";
        }

        m_audioCurrentPref.format = audioFrameFormat;
        m_audioCurrentPref.channels = audioFrameChannels;
        m_audioCurrentPref.channel_layout = dec_channel_layout;
        m_audioCurrentPref.frequency = (audioFrame->sample_rate * speed.numerator) / speed.denominator;
    }
}

bool FFmpegDecoder::handleAudioFrame(
    double frame_clock, uint8_t* write_data, int64_t write_size, bool failed)
{
    bool skipAll = false;
    double delta = 0;
    bool isPaused = false;

    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
        isPaused = m_isPaused;
        if (!isPaused)
        {
            delta = (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
                ? GetHiResTime() - m_videoStartClock - m_audioPTS : 0;
        }
    }

    if (isPaused)
    {
        if (!m_audioPaused)
        {
            m_audioPlayer->WaveOutPause();
            m_audioPaused = true;
        }

        const bool hasVideo = m_mainVideoThread != nullptr;

        boost::unique_lock<boost::mutex> locker(m_isPausedMutex);

        while (m_isVideoSeekingWhilePaused && hasVideo
            || (delta = (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
                ? (m_isPaused ? m_pauseTimer : GetHiResTime()) - m_videoStartClock - m_audioPTS : 0
                , m_isPaused && !(skipAll = delta >= frame_clock)))
        {
            m_isPausedCV.wait(locker);
        }
    }
    else if (delta > 1 && m_formatContexts.size() > 1 && delta > frame_clock)
    {
        CHANNEL_LOG(ffmpeg_sync) << "Skip audio frame";
        skipAll = true;
    }

    if (!skipAll && m_mainVideoThread != nullptr
        && std::all_of(write_data, write_data + write_size, [](uint8_t data) { return data == 0; })
        //&& m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED
        && m_videoPacketsQueue.empty()
        && (boost::lock_guard<boost::mutex>(m_videoFramesMutex), !m_videoFramesQueue.canPop()))
    {
        return true; // just ignore?
    }

    // Audio sync
    if (!failed && !skipAll && fabs(delta) > 0.1)
    {
        CHANNEL_LOG(ffmpeg_sync) << "Audio sync delta = " << delta;
        InterlockedAdd(m_videoStartClock, delta / 2);
    }

    if (m_audioPaused && !skipAll)
    {
        m_audioPlayer->WaveOutRestart();
        m_audioPaused = false;
    }

    boost::this_thread::interruption_point();

    if (skipAll)
    {
        InterlockedAdd(m_audioPTS, frame_clock);
    }
    else if (!(m_audioPlayer->WriteAudio(write_data, write_size)
        || initAudioOutput() && m_audioPlayer->WriteAudio(write_data, write_size)))
    {
        return false;
    }

    return true;
}
