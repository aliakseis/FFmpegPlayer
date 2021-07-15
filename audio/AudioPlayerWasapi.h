#pragma once

#include "../video/audioplayer.h"

#include <atlbase.h>

struct IAudioClient;
struct IAudioRenderClient;
struct ISimpleAudioVolume;

class AudioPlayerWasapi :
    public IAudioPlayer
{
public:
    AudioPlayerWasapi();
    ~AudioPlayerWasapi() override;

    AudioPlayerWasapi(const AudioPlayerWasapi&) = delete;
    AudioPlayerWasapi& operator=(const AudioPlayerWasapi&) = delete;

    void SetCallback(IAudioPlayerCallback* callback) override
    {
        m_callback = callback;
    }

    void InitializeThread() override;
    void DeinitializeThread() override;

    void WaveOutReset() override;

    void Close() override;
    bool Open(int bytesPerSample, int channels, int* samplesPerSec) override;

    void SetVolume(double volume) override;
    double GetVolume() const override;

    void WaveOutPause() override;
    void WaveOutRestart() override;

    bool WriteAudio(uint8_t* write_data, int64_t write_size) override;

private:
    IAudioPlayerCallback* m_callback;

    HANDLE m_hAudioSamplesRenderEvent;

    CComPtr<IAudioClient> m_AudioClient;
    CComPtr<IAudioRenderClient> m_RenderClient;
    CComPtr<ISimpleAudioVolume> m_SimpleAudioVolume;

    unsigned int m_BufferSize;
    unsigned int m_FrameSize;
    unsigned int m_samplesPerSec;

    HANDLE m_mmcssHandle;

    bool m_coUnitialize;
};
