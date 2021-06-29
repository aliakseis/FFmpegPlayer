#include "AudioPitchDecorator.h"

#include "smbPitchShift.h"

#include <algorithm>
#include <utility>

AudioPitchDecorator::AudioPitchDecorator(std::unique_ptr<IAudioPlayer> player
    , std::function<float()> getPitchShift)
    : m_player(std::move(player))
    , m_getPitchShift(std::move(getPitchShift))
{
}

AudioPitchDecorator::~AudioPitchDecorator() = default;

void AudioPitchDecorator::SetCallback(IAudioPlayerCallback * callback)
{
    m_player->SetCallback(callback);
}

void AudioPitchDecorator::InitializeThread()
{
    m_player->InitializeThread();
}

void AudioPitchDecorator::DeinitializeThread()
{
    m_player->DeinitializeThread();
}

void AudioPitchDecorator::WaveOutReset()
{
    m_player->WaveOutReset();
    for (auto& v : m_smbPitchShifts)
        v.reset();
}

void AudioPitchDecorator::Close()
{
    m_player->Close();
}

bool AudioPitchDecorator::Open(int bytesPerSample, int channels, int * samplesPerSec)
{
    if (!m_player->Open(bytesPerSample, channels, samplesPerSec))
        return false;

    m_bytesPerSample = bytesPerSample;
    m_samplesPerSec = *samplesPerSec;
    m_smbPitchShifts.resize(channels);
    for (auto& v : m_smbPitchShifts)
        v.reset();

    return true;
}

void AudioPitchDecorator::Reset()
{
    m_player->Reset();
}

void AudioPitchDecorator::SetVolume(double volume)
{
    m_player->SetVolume(volume);
}

double AudioPitchDecorator::GetVolume() const
{
    return m_player->GetVolume();
}

void AudioPitchDecorator::WaveOutPause()
{
    m_player->WaveOutPause();
}

void AudioPitchDecorator::WaveOutRestart()
{
    m_player->WaveOutRestart();
}

bool AudioPitchDecorator::WriteAudio(uint8_t * write_data, int64_t write_size)
{
    const auto pitchShift = m_getPitchShift();
    if (pitchShift != 1. && m_bytesPerSample == 2)
    {
        const auto numSamples = (write_size / m_bytesPerSample) / m_smbPitchShifts.size();
        if (m_buffer.size() < numSamples)
            m_buffer.resize(numSamples);
        int16_t* const intData = (int16_t*)write_data;
        for (int i = 0; i < m_smbPitchShifts.size(); ++i)
        {
            for (size_t j = 0; j < numSamples; ++j)
            {
                m_buffer[j] = intData[j * m_smbPitchShifts.size() + i] / 32768.;
            }
            m_smbPitchShifts[i].smbPitchShift(
                pitchShift, numSamples, 4096, 16, m_samplesPerSec, m_buffer.data(), m_buffer.data());
            for (size_t j = 0; j < numSamples; ++j)
            {
                // decrease level to avoid clipping distortions
                intData[j * m_smbPitchShifts.size() + i] = std::clamp(m_buffer[j], -2.f, 2.f) * (32767. / 2);
            }
        }
    }
    else
    {
        for (auto& v : m_smbPitchShifts)
            v.reset();
    }
    return m_player->WriteAudio(write_data, write_size);
}
