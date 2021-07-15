#pragma once

#include "../video/audioplayer.h"

#include <functional>
#include <memory>
#include <vector>

class CSmbPitchShift;

class AudioPitchDecorator :
    public IAudioPlayer
{
public:
    AudioPitchDecorator(std::unique_ptr<IAudioPlayer> player, 
        std::function<float()> getPitchShift);
    ~AudioPitchDecorator();

    AudioPitchDecorator(const AudioPitchDecorator&) = delete;
    AudioPitchDecorator& operator =(const AudioPitchDecorator&) = delete;

    // Inherited via IAudioPlayer
    void SetCallback(IAudioPlayerCallback * callback) override;
    void InitializeThread() override;
    void DeinitializeThread() override;
    void WaveOutReset() override;
    void Close() override;
    bool Open(int bytesPerSample, int channels, int * samplesPerSec) override;
    void SetVolume(double volume) override;
    double GetVolume() const override;
    void WaveOutPause() override;
    void WaveOutRestart() override;
    bool WriteAudio(uint8_t * write_data, int64_t write_size) override;

private:
    std::unique_ptr<IAudioPlayer> m_player;
    std::function<float()> m_getPitchShift;
    std::vector<CSmbPitchShift> m_smbPitchShifts;

    std::vector<float> m_buffer;

    int m_bytesPerSample{};
    int m_samplesPerSec{};
};

