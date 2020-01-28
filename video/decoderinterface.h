#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
using PathType = std::wstring;
#else
using PathType = std::string;
#endif

struct IDirect3DDevice9;
struct IDirect3DSurface9;

struct IFrameDecoder;

struct FrameRenderingData
{
    uint8_t** image{};
    const int* pitch{};
    int width; 
    int height;
    int aspectNum;
    int aspectDen;

    IDirect3DDevice9* d3d9device{};
    IDirect3DSurface9** surface{};
};

struct IFrameListener
{
    virtual ~IFrameListener() = default;
    virtual void updateFrame() = 0;
    virtual void drawFrame(IFrameDecoder* decoder, unsigned int generation) = 0; // decoder->finishedDisplayingFrame() must be called
    virtual void decoderClosing() = 0;
};

struct FrameDecoderListener
{
    virtual ~FrameDecoderListener() = default;

    virtual void changedFramePosition(
        long long /*start*/, long long /*frame*/, long long /*total*/) {}
    virtual void decoderClosed(bool /*fileReleased*/) {}
    virtual void fileLoaded(long long /*start*/, long long /*total*/) {}
    virtual void volumeChanged(double /*volume*/) {}

    virtual void onEndOfStream() {}

    virtual void playingFinished() {}
};

struct RationalNumber
{
    int numerator;
    int denominator;
};

inline bool operator == (const RationalNumber& left, const RationalNumber& right)
{
    return left.numerator == right.numerator && left.denominator == right.denominator;
}

struct IFrameDecoder
{
    enum FrameFormat {
        PIX_FMT_YUV420P,   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
        PIX_FMT_YUYV422,   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
        PIX_FMT_RGB24,     ///< packed RGB 8:8:8, 24bpp, RGBRGB...
    };

    virtual ~IFrameDecoder() = default;

    virtual void SetFrameFormat(FrameFormat format, bool allowDirect3dData) = 0;

    virtual bool openFile(const PathType& file) = 0;
    virtual bool openUrl(const std::string& url) = 0;

    virtual void play(bool isPaused = false) = 0;
    virtual bool pauseResume() = 0;
    virtual bool nextFrame() = 0;
    virtual void setVolume(double volume) = 0;

    virtual bool seekByPercent(double percent) = 0;

    virtual void videoReset() = 0;

    virtual void setFrameListener(IFrameListener* listener) = 0;
    virtual void setDecoderListener(FrameDecoderListener* listener) = 0;
    virtual bool getFrameRenderingData(FrameRenderingData* data) = 0;
    virtual void finishedDisplayingFrame(unsigned int generation) = 0;

    virtual void close() = 0;

    virtual bool isPlaying() const = 0;
    virtual bool isPaused() const = 0;
    virtual double volume() const = 0;
    virtual double getDurationSecs(int64_t duration) const = 0;

    virtual int getNumAudioTracks() const = 0;
    virtual int getAudioTrack() const = 0;
    virtual void setAudioTrack(int idx) = 0;

    virtual RationalNumber getSpeedRational() const = 0;
    virtual void setSpeedRational(const RationalNumber& speed) = 0;

    virtual std::vector<std::string> getProperties() = 0;
};

struct IAudioPlayer;

std::unique_ptr<IFrameDecoder> GetFrameDecoder(std::unique_ptr<IAudioPlayer> audioPlayer);
