#pragma once

#include "audioplayer.h"

#include <mmsystem.h>

class AudioPlayerImpl :
    public IAudioPlayer
{
public:
    AudioPlayerImpl();
    ~AudioPlayerImpl() override;

    AudioPlayerImpl(const AudioPlayerImpl&) = delete;
    AudioPlayerImpl& operator=(const AudioPlayerImpl&) = delete;

    void SetCallback(IAudioPlayerCallback* callback) override
    {
        m_callback = callback;
    }

    void InitializeThread() override;
    void DeinitializeThread() override;

    void WaveOutReset() override;

    void Close() override;
    bool Open(int bytesPerSample, int channels, int* samplesPerSec) override;
    void Reset() override;

    void SetVolume(double volume) override;
    double GetVolume() const override;

    void WaveOutPause() override;
    void WaveOutRestart() override;

    bool WriteAudio(uint8_t* write_data, int64_t write_size) override;

private:
    IAudioPlayerCallback* m_callback;

    HWAVEOUT			m_waveOutput;

    static void CALLBACK waveOutProc(HWAVEOUT, UINT, DWORD, DWORD, DWORD);

    WAVEHDR*			m_waveBlocks;
    volatile long		m_waveFreeBlockCount {};
    HANDLE				m_evtHasFreeBlocks;
    int					m_waveCurrentBlock {};

    int					m_bytesPerSecond;
};

