#include "portaudioplayer.h"

#include <portaudio.h>
#include <QThread>

PortAudioPlayer::PortAudioPlayer()
{
    auto err = Pa_Initialize();
}

PortAudioPlayer::~PortAudioPlayer()
{
    auto err = Pa_Terminate();
}

void PortAudioPlayer::InitializeThread()
{
    QThread::currentThread()->setPriority(QThread::TimeCriticalPriority);
}

bool PortAudioPlayer::Open(int bytesPerSample, int channels, int* samplesPerSec)
{
    PaStreamParameters params{};
    params.device = Pa_GetDefaultOutputDevice();
    params.suggestedLatency = Pa_GetDeviceInfo(params.device)->defaultHighOutputLatency;
    params.channelCount = channels;

    switch (bytesPerSample)
    {
    case 1:
        params.sampleFormat = paInt8;
        break;
    case 2:
        params.sampleFormat = paInt16;
        break;
    case 4:
        params.sampleFormat = paInt32;
        break;
    }

    auto err{ Pa_OpenStream(&m_stream, nullptr, &params, *samplesPerSec, paFramesPerBufferUnspecified,
        paNoFlag, nullptr, nullptr) };

    if (err != paNoError)
        return false;

    err = Pa_StartStream(m_stream);

    m_FrameSize = bytesPerSample * channels;

    *samplesPerSec = m_samplesPerSec = Pa_GetStreamInfo(m_stream)->sampleRate;

    return true;
}

void PortAudioPlayer::Close()
{
    auto err = Pa_CloseStream(m_stream);
    m_stream = nullptr;
}

bool PortAudioPlayer::WriteAudio(uint8_t* write_data, int64_t write_size)
{
    if (!m_stream)
        return false;

    int16_t* realData = (int16_t*)write_data;
    for (unsigned int i = 0; i < write_size / 2; ++i)
        realData[i] *= m_volume;

    const auto framesToWrite = write_size / m_FrameSize;
    auto err = Pa_WriteStream(m_stream, write_data, framesToWrite);

    // count audio pts
    const double frame_clock = (double)framesToWrite / m_samplesPerSec;
    m_callback->AppendFrameClock(frame_clock);

    return err == paNoError;
}

void PortAudioPlayer::WaveOutReset()
{
    Pa_AbortStream(m_stream);
}

void PortAudioPlayer::WaveOutPause()
{
    Pa_StopStream(m_stream);
}

void PortAudioPlayer::WaveOutRestart()
{
    Pa_StartStream(m_stream);
}
