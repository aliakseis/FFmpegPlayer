#include "stdafx.h"
#include "AudioPitchDecorator.h"

#include "smbPitchShift.h"

#include <algorithm>
#include <utility>

AudioPitchDecorator::AudioPitchDecorator(std::unique_ptr<IAudioPlayer> player)
    : m_player(std::move(player))
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
    if (m_bytesPerSample == 2)
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
                2., numSamples, 4096, 32, m_samplesPerSec, m_buffer.data(), m_buffer.data());
            for (size_t j = 0; j < numSamples; ++j)
            {
                intData[j * m_smbPitchShifts.size() + i] = std::clamp(m_buffer[j], -1.f, 1.f) * 32767;
            }
        }
    }
    return m_player->WriteAudio(write_data, write_size);
}
