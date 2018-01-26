#include "ffmpegdecoder.h"
#include "makeguard.h"

#include <boost/log/trivial.hpp>

#include <functional>
#include <memory>

namespace {

uint8_t** getAudioData(AVFrame* audioFrame)
{
    return audioFrame->extended_data
        ? audioFrame->extended_data
        : &audioFrame->data[0];
}

int64_t getChannelLayout(AVFrame* audioFrame)
{
    const int audioFrameChannels = av_frame_get_channels(audioFrame);
    return (audioFrame->channel_layout &&
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

    m_audioPlayer->InitializeThread();
    auto deinitializeThread = MakeGuard(
        m_audioPlayer.get(),
        std::mem_fn(&IAudioPlayer::DeinitializeThread));

    std::vector<uint8_t> resampleBuffer;

    for (;;)
    {
        if (m_isPaused)
        {
            if (!m_audioPaused)
            {
                m_audioPlayer->WaveOutPause();
            }
            m_audioPaused = true;

            boost::this_thread::interruption_point();

            boost::unique_lock<boost::mutex> locker(m_isPausedMutex);
            while (m_isPaused)
            {
                m_isPausedCV.wait(locker);
            }
        }

        if (m_audioPaused)
        {
            m_audioPlayer->WaveOutRestart();
            m_audioPaused = false;
        }

        for (;;)
        {
            if (!m_audioPacketsQueue.pop(packet,
                [this] { return static_cast<bool>(m_isPaused); }))
            {
                break;
            }

            auto packetGuard = MakeGuard(&packet, av_packet_unref);

            if (!initialized)
            {
                if (packet.pts != AV_NOPTS_VALUE)
                {
                    const double pts = av_q2d(m_audioStream->time_base) * packet.pts;
                    m_audioPTS = pts;
                }
                else
                {
                    assert(false && "No audio pts found");
                    return;
                }

                // invoke changedFramePosition() if needed
                //AppendFrameClock(0);
            }

            initialized = true;

            const bool handled = handleAudioPacket(packet, resampleBuffer);
            if (!handled || m_isPaused)
            {
                break;
            }
        }

        // Break thread
        boost::this_thread::interruption_point();
    }
}

bool FFmpegDecoder::handleAudioPacket(
    const AVPacket& packet,
    std::vector<uint8_t>& resampleBuffer)
{
    if (packet.stream_index != m_audioStream->index)
    {
        avcodec_close(m_audioCodecContext);
        m_audioStream = m_formatContext->streams[packet.stream_index];
        if (!setupAudioCodec())
        {
            return false;
        }
    }

    const int ret = avcodec_send_packet(m_audioCodecContext, &packet);
    if (ret < 0)
        return false;

    while (avcodec_receive_frame(m_audioCodecContext, m_audioFrame) == 0)
    {
        if (m_audioFrame->nb_samples <= 0)
        {
            continue;
        }

        const AVSampleFormat audioFrameFormat = (AVSampleFormat)m_audioFrame->format;
        const int audioFrameChannels = av_frame_get_channels(m_audioFrame);

        const int original_buffer_size = av_samples_get_buffer_size(
            nullptr, audioFrameChannels,
            m_audioFrame->nb_samples, audioFrameFormat, 1);

        // write buffer
        uint8_t* write_data = *getAudioData(m_audioFrame);
        int64_t write_size = original_buffer_size;

        const int64_t dec_channel_layout = getChannelLayout(m_audioFrame);

        // Check if the new swr context required
        if (audioFrameFormat != m_audioCurrentPref.format ||
            dec_channel_layout != m_audioCurrentPref.channel_layout ||
            m_audioFrame->sample_rate != m_audioCurrentPref.frequency)
        {
            swr_free(&m_audioSwrContext);
            m_audioSwrContext = swr_alloc_set_opts(
                nullptr, m_audioSettings.channel_layout, m_audioSettings.format,
                m_audioSettings.frequency, dec_channel_layout, audioFrameFormat,
                m_audioFrame->sample_rate, 0, nullptr);

            if (!m_audioSwrContext || swr_init(m_audioSwrContext) < 0)
            {
                BOOST_LOG_TRIVIAL(error) << "unable to initialize swr convert context";
            }

            m_audioCurrentPref.format = audioFrameFormat;
            m_audioCurrentPref.channels = audioFrameChannels;
            m_audioCurrentPref.channel_layout = dec_channel_layout;
            m_audioCurrentPref.frequency = m_audioFrame->sample_rate;
        }

        if (m_audioSwrContext)
        {
            enum { EXTRA_SPACE = 256 };

            const int out_count = (int64_t)m_audioFrame->nb_samples *
                m_audioSettings.frequency /
                m_audioFrame->sample_rate + EXTRA_SPACE;

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
                const_cast<const uint8_t**>(getAudioData(m_audioFrame)),
                m_audioFrame->nb_samples);

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

        // Audio sync
        const double delta = m_videoStartClock + m_audioPTS - GetHiResTime();
        if (fabs(delta) > 0.1)
        {
            //const double correction = (delta < 0) ? 0.05 : -0.05;
            const double correction = -delta / 2;
            for (double v = m_videoStartClock;
                 !m_videoStartClock.compare_exchange_weak(v, v + correction);)
            {
            }
        }

        if (write_size > 0)
        {
            if (boost::this_thread::interruption_requested())
            {
                return false;
            }

            if (!m_audioPlayer->WriteAudio(write_data, write_size) &&
                m_audioFrame->sample_rate)
            {
                const double frame_clock =
                    (double)original_buffer_size / (audioFrameChannels *
                                                    m_audioFrame->sample_rate *
                                                    av_get_bytes_per_sample(audioFrameFormat));

                for (double v = m_audioPTS;
                     !m_audioPTS.compare_exchange_weak(v, v + frame_clock);)
                {
                }
            }
        }
    }

    return true;
}
