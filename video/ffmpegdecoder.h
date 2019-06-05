#pragma once

#include "decoderinterface.h"
#include "audioplayer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
//#include <libavdevice/avdevice.h>
}

#include <string>
#include <boost/thread/thread.hpp>
#include <boost/atomic.hpp>
#include <memory>
#include <vector>

#include <boost/chrono.hpp>
#include <boost/log/sources/channel_logger.hpp>
#include <boost/log/common.hpp>

namespace channel_logger
{

extern boost::log::sources::channel_logger_mt<> 
    ffmpeg_closing, 
    ffmpeg_opening, 
    ffmpeg_pause,
    ffmpeg_seek, 
    ffmpeg_sync,
    ffmpeg_threads, 
    ffmpeg_volume,
    ffmpeg_internal;

} // namespace channel_logger

#define CHANNEL_LOG(channel) BOOST_LOG(::channel_logger::channel)

#include "fqueue.h"
#include "videoframe.h"
#include "vqueue.h"


// Inspired by http://dranger.com/ffmpeg/ffmpeg.html

class FFmpegDecoder : public IFrameDecoder, public IAudioPlayerCallback
{
   public:
    FFmpegDecoder(std::unique_ptr<IAudioPlayer> audioPlayer);
    ~FFmpegDecoder();

    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    void SetFrameFormat(FrameFormat format, bool allowDirect3dData) override;

    bool openFile(const PathType& file) override;
    bool openUrl(const std::string& url) override;
    bool seekDuration(int64_t duration);
    bool seekByPercent(double percent) override;

    void videoReset() override;

    double volume() const override;

    inline bool isPlaying() const override { return m_isPlaying; }
    inline bool isPaused() const override { return m_isPaused; }

    void setFrameListener(IFrameListener* listener) override { m_frameListener = listener; }

    void setDecoderListener(FrameDecoderListener* listener) override
    {
        m_decoderListener = listener;
    }

    bool getFrameRenderingData(FrameRenderingData* data) override;

    double getDurationSecs(int64_t duration) const override
    {
        return av_q2d((m_videoStream != nullptr)? m_videoStream->time_base : m_audioStream->time_base) * duration;
    }

    void finishedDisplayingFrame(unsigned int generation) override;

    void close() override;
    void play(bool isPaused = false) override;
    bool pauseResume() override;
    bool nextFrame() override;
    void setVolume(double volume) override;

    int getNumAudioTracks() const override;
    int getAudioTrack() const override;
    void setAudioTrack(int idx) override;

    std::pair<int, int> getSpeedRational() const override;
    void setSpeedRational(const std::pair<int, int>& speed) override;

   private:
    class IOContext;
    struct VideoParseContext;

    // Threads
    void parseRunnable();
    void audioParseRunnable();
    void videoParseRunnable();
    void displayRunnable();

    void dispatchPacket(AVPacket& packet);
    void startAudioThread();
    void startVideoThread();
    //void seek();
    bool resetDecoding(int64_t seekDuration, bool resetVideo);
    void fixDuration();
    bool handleAudioPacket(
        const AVPacket& packet,
        std::vector<uint8_t>& resampleBuffer,
        bool failed);
    bool handleVideoPacket(
        const AVPacket& packet,
        double& videoClock,
        VideoParseContext& context);

    // IAudioPlayerCallback
    void AppendFrameClock(double frame_clock) override;

    void resetVariables();
    void closeProcessing();

    bool openDecoder(const PathType& file, const std::string& url, bool isFile);

    bool resetVideoProcessing();
    bool setupAudioProcessing();
    bool setupAudioCodec();
    bool initAudioOutput();

    void seekWhilePaused();

    void handleDirect3dData(AVFrame* videoFrame);

    double GetHiResTime();

    // Frame display listener
    IFrameListener* m_frameListener;

    FrameDecoderListener* m_decoderListener;

    // Indicators
    bool m_isPlaying;

    std::unique_ptr<boost::thread> m_mainVideoThread;
    std::unique_ptr<boost::thread> m_mainAudioThread;
    std::unique_ptr<boost::thread> m_mainParseThread;
    std::unique_ptr<boost::thread> m_mainDisplayThread;

    // Synchronization
    boost::atomic<double> m_audioPTS;

    // Real duration from video stream
    int64_t m_startTime;
    boost::atomic_int64_t m_currentTime;
    int64_t m_duration;

    // Basic stuff
    AVFormatContext* m_formatContext;

    boost::atomic_int64_t m_seekDuration;
    boost::atomic_int64_t m_videoResetDuration;

    boost::atomic_bool m_videoResetting;

    // Video Stuff
    enum { VIDEO_START_CLOCK_NOT_INITIALIZED = -1000000000 };
    boost::atomic<double> m_videoStartClock;

    AVCodec* m_videoCodec;
    AVCodecContext* m_videoCodecContext;
    AVStream* m_videoStream;
    int m_videoStreamNumber;

    // Audio Stuff
    AVCodec* m_audioCodec;
    AVCodecContext* m_audioCodecContext;
    AVStream* m_audioStream;
    boost::atomic<int> m_audioStreamNumber;
    SwrContext* m_audioSwrContext;

    struct AudioParams
    {
        int frequency;
        int channels;
        int64_t channel_layout;
        AVSampleFormat format;
    };
    AudioParams m_audioSettings;
    AudioParams m_audioCurrentPref;

    // Stuff for converting image
    SwsContext* m_imageCovertContext;
    AVPixelFormat m_pixelFormat;
    bool m_allowDirect3dData;

    // Video and audio queues
    enum
    {
        MAX_QUEUE_SIZE = (15 * 1024 * 1024),
        MAX_VIDEO_FRAMES = 200,
        MAX_AUDIO_FRAMES = 100,
    };
    FQueue<MAX_QUEUE_SIZE, MAX_VIDEO_FRAMES> m_videoPacketsQueue;
    FQueue<MAX_QUEUE_SIZE, MAX_AUDIO_FRAMES> m_audioPacketsQueue;

    VQueue m_videoFramesQueue;

    bool m_frameDisplayingRequested;

    unsigned int m_generation;

    boost::mutex m_videoFramesMutex;
    boost::condition_variable m_videoFramesCV;

    boost::atomic_bool m_isPaused;
    boost::mutex m_isPausedMutex;
    boost::condition_variable m_isPausedCV;
    double m_pauseTimer;

    boost::atomic_bool m_isVideoSeekingWhilePaused;

    // Audio
    std::unique_ptr<IAudioPlayer> m_audioPlayer;
    bool m_audioPaused;

    std::vector<int> m_audioIndices;

    std::unique_ptr<IOContext> m_ioCtx;

    boost::atomic<boost::chrono::high_resolution_clock::duration> m_referenceTime;

    boost::atomic<std::pair<int, int>> m_speedRational; // Numerator, Denominator
};
