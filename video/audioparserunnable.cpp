#include "audioparserunnable.h"
#include "makeguard.h"

#include <boost/log/trivial.hpp>

#include <functional>
#include <memory>

bool AudioParseRunnable::getAudioPacket(AVPacket& packet)
{
    return m_ffmpeg->m_audioPacketsQueue.pop(
        packet,
        [this] { return m_ffmpeg->m_isPaused && !m_ffmpeg->m_isAudioSeekingWhilePaused; }
    );
}

void AudioParseRunnable::operator()()
{
    CHANNEL_LOG(ffmpeg_threads) << "Audio thread started";
    AVPacket packet;

    bool initialized = false;

    bool handlePacketPostponed = false;

    bool aPauseDisabled = false;

    m_ffmpeg->m_audioPlayer->InitializeThread();
    auto deinitializeThread = MakeGuard(
        m_ffmpeg->m_audioPlayer.get(),
        std::mem_fn(&IAudioPlayer::DeinitializeThread));

    std::vector<uint8_t> resampleBuffer;

    try
    {
        for (;;)
        {
            if (m_ffmpeg->m_isPaused && !m_ffmpeg->m_isAudioSeekingWhilePaused)
            {
                m_ffmpeg->m_audioPlayer->WaveOutPause();
                aPauseDisabled = true;

                boost::this_thread::interruption_point();

                boost::unique_lock<boost::mutex> locker(m_ffmpeg->m_isPausedMutex);
                while (m_ffmpeg->m_isPaused)
                {
                    m_ffmpeg->m_isPausedCV.wait(locker);
                }
                continue;
            }

            if (aPauseDisabled && !m_ffmpeg->m_isAudioSeekingWhilePaused)
            {
                m_ffmpeg->m_audioPlayer->WaveOutRestart();
                aPauseDisabled = false;
            }

            if (handlePacketPostponed)
            {
                handlePacketPostponed = false;
                if (m_ffmpeg->m_isAudioSeekingWhilePaused)
                {
                    av_packet_unref(&packet);
                }
                else if (!handlePacket(packet, resampleBuffer))
                {
                    av_packet_unref(&packet);
                    break;
                }
            }

            for (;;)
            {
                if (!getAudioPacket(packet))
                {
                    break;
                }

                if (!initialized)
                {
                    if (packet.pts != AV_NOPTS_VALUE)
                    {
                        const double pts = av_q2d(m_ffmpeg->m_audioStream->time_base) * packet.pts;
                        m_ffmpeg->m_audioPTS = pts;
                    }
                    else
                    {
                        assert(false && "No audio pts found");
                        return;
                    }

                    // invoke changedFramePosition() if needed
                    m_ffmpeg->AppendFrameClock(0);

                    if (m_ffmpeg->m_isAudioSeekingWhilePaused)
                    {
                        m_ffmpeg->m_isAudioSeekingWhilePaused = false;
                        handlePacketPostponed = true;

                        break;
                    }
                }

                initialized = true;

                if (packet.size == 0)
                {
                    CHANNEL_LOG(ffmpeg_audio) << "Packet size = 0";
                    break;
                }

                const bool handled = handlePacket(packet, resampleBuffer);
                av_packet_unref(&packet);

                if (!handled || m_ffmpeg->m_isPaused && !m_ffmpeg->m_isAudioSeekingWhilePaused)
                {
                    break;
                }
            }

            // Break thread
            boost::this_thread::interruption_point();
        }
    }
    catch (...)
    {
        if (handlePacketPostponed)
        {
            av_packet_unref(&packet);
        }
        CHANNEL_LOG(ffmpeg_threads) << "Audio thread interrupted";
        throw;
    }
}

bool AudioParseRunnable::handlePacket(AVPacket packet,  // uses copy
                                      std::vector<uint8_t>& resampleBuffer)
{
    while (packet.size > 0)
    {
        int audioDecoded = 0;
        const int length = avcodec_decode_audio4(m_ffmpeg->m_audioCodecContext,
                                                 m_ffmpeg->m_audioFrame, &audioDecoded, &packet);
        if (length < 0)
        {
            // Broken packet
            break;  // Simply skip frame without errors
        }
        packet.size -= length;
        packet.data += length;

        if (!audioDecoded || m_ffmpeg->m_audioFrame->nb_samples <= 0)
        {
            continue;
        }

        const AVSampleFormat audioFrameFormat = (AVSampleFormat)m_ffmpeg->m_audioFrame->format;

        const int original_buffer_size = av_samples_get_buffer_size(
            nullptr, av_frame_get_channels(m_ffmpeg->m_audioFrame),
            m_ffmpeg->m_audioFrame->nb_samples, audioFrameFormat, 1);

        // write buffer
        uint8_t* write_data = (m_ffmpeg->m_audioFrame->extended_data)
                                  ? *m_ffmpeg->m_audioFrame->extended_data
                                  : m_ffmpeg->m_audioFrame->data[0];
        int64_t write_size = original_buffer_size;

        const int64_t dec_channel_layout =
            (m_ffmpeg->m_audioFrame->channel_layout &&
             av_frame_get_channels(m_ffmpeg->m_audioFrame) ==
                 av_get_channel_layout_nb_channels(m_ffmpeg->m_audioFrame->channel_layout))
                ? m_ffmpeg->m_audioFrame->channel_layout
                : av_get_default_channel_layout(av_frame_get_channels(m_ffmpeg->m_audioFrame));

        // Check if the new swr context required
        if (audioFrameFormat != m_ffmpeg->m_audioCurrentPref.format ||
            dec_channel_layout != m_ffmpeg->m_audioCurrentPref.channel_layout ||
            m_ffmpeg->m_audioFrame->sample_rate != m_ffmpeg->m_audioCurrentPref.frequency)
        {
            swr_free(&m_ffmpeg->m_audioSwrContext);
            m_ffmpeg->m_audioSwrContext = swr_alloc_set_opts(
                nullptr, m_ffmpeg->m_audioSettings.channel_layout, m_ffmpeg->m_audioSettings.format,
                m_ffmpeg->m_audioSettings.frequency, dec_channel_layout, audioFrameFormat,
                m_ffmpeg->m_audioFrame->sample_rate, 0, nullptr);

            if (!m_ffmpeg->m_audioSwrContext || swr_init(m_ffmpeg->m_audioSwrContext) < 0)
            {
                BOOST_LOG_TRIVIAL(error) << "unable to initialize swr convert context";
            }

            m_ffmpeg->m_audioCurrentPref.format = audioFrameFormat;
            m_ffmpeg->m_audioCurrentPref.channels = av_frame_get_channels(m_ffmpeg->m_audioFrame);
            m_ffmpeg->m_audioCurrentPref.channel_layout = dec_channel_layout;
            m_ffmpeg->m_audioCurrentPref.frequency = m_ffmpeg->m_audioFrame->sample_rate;
        }

        if (m_ffmpeg->m_audioSwrContext)
        {
            enum { EXTRA_SPACE = 256 };

            const int out_count = (int64_t)m_ffmpeg->m_audioFrame->nb_samples *
                m_ffmpeg->m_audioSettings.frequency /
                m_ffmpeg->m_audioFrame->sample_rate + EXTRA_SPACE;

            const int size_multiplier = m_ffmpeg->m_audioSettings.channels *
                av_get_bytes_per_sample(m_ffmpeg->m_audioSettings.format);

            const size_t buffer_size = out_count * size_multiplier;

            if (resampleBuffer.size() < buffer_size)
            {
                resampleBuffer.resize(buffer_size);
            }

            // Code for resampling
            uint8_t *out = resampleBuffer.data();
            const int converted_size = swr_convert(
                m_ffmpeg->m_audioSwrContext, 
                &out,
                out_count,
                const_cast<const uint8_t**>(m_ffmpeg->m_audioFrame->extended_data
                    ? m_ffmpeg->m_audioFrame->extended_data
                    : &m_ffmpeg->m_audioFrame->data[0]),
                m_ffmpeg->m_audioFrame->nb_samples);

            if (converted_size < 0)
            {
                BOOST_LOG_TRIVIAL(error) << "swr_convert() failed";
                break;
            }

            if (converted_size == out_count)
            {
                BOOST_LOG_TRIVIAL(warning) << "audio buffer is probably too small";
                swr_init(m_ffmpeg->m_audioSwrContext);
            }

            write_data = out;
            write_size = converted_size * size_multiplier;

            assert(write_size < buffer_size);
        }

        // Audio sync
        const double delta = m_ffmpeg->m_videoStartClock + m_ffmpeg->m_audioPTS - GetHiResTime();
        if (fabs(delta) > 0.1)
        {
            const double correction = (delta < 0) ? 0.05 : -0.05;
            for (double v = m_ffmpeg->m_videoStartClock;
                 !m_ffmpeg->m_videoStartClock.compare_exchange_weak(v, v + correction);)
            {
            }
        }

        if (write_size > 0)
        {
            if (boost::this_thread::interruption_requested())
            {
                return false;
            }

            if (!m_ffmpeg->m_audioPlayer->WriteAudio(write_data, write_size) &&
                m_ffmpeg->m_audioFrame->sample_rate)
            {
                const double frame_clock =
                    (double)original_buffer_size / (av_frame_get_channels(m_ffmpeg->m_audioFrame) *
                                                    m_ffmpeg->m_audioFrame->sample_rate *
                                                    av_get_bytes_per_sample(audioFrameFormat));

                for (double v = m_ffmpeg->m_audioPTS;
                     !m_ffmpeg->m_audioPTS.compare_exchange_weak(v, v + frame_clock);)
                {
                }
            }
        }
    }

    return true;
}
