#pragma once

#include "../video/audioplayer.h"


class PortAudioPlayer :
    public IAudioPlayer
{
public:
    PortAudioPlayer();
    ~PortAudioPlayer() override;

    PortAudioPlayer(const PortAudioPlayer&) = delete;
    PortAudioPlayer& operator=(const PortAudioPlayer&) = delete;

    void SetCallback(IAudioPlayerCallback* callback) override
    {
        m_callback = callback;
    }

    void InitializeThread() override {}
    void DeinitializeThread() override {}

    void WaveOutReset() override;

    void Close() override;
    bool Open(int bytesPerSample, int channels, int* samplesPerSec) override;

    void SetVolume(double volume) override { m_volume = volume; }
    double GetVolume() const override { return m_volume; }

    void WaveOutPause() override;
    void WaveOutRestart() override;

    bool WriteAudio(uint8_t* write_data, int64_t write_size) override;

private:
    IAudioPlayerCallback* m_callback{};
    void* m_stream{};

    int m_FrameSize{};
    double m_samplesPerSec{};
    double m_volume = 1.;
};
