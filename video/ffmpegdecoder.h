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
#include <boost/smart_ptr/atomic_shared_ptr.hpp>
#include <memory>
#include <vector>

#include <boost/chrono.hpp>
#include <boost/log/sources/channel_logger.hpp>
#include <boost/log/common.hpp>

namespace channel_logger
{

// Weird x64 debug build crash fixed

#if defined(_MSC_VER) && _MSC_VER < 1910

class ChannelLogger : public boost::log::sources::channel_logger_mt<>
{
    template<typename... T>
    auto open_record_unlocked(basic_logger<T...>*)
    {
        return basic_logger<T...>::open_record_unlocked();
    }
public:
    using channel_logger_mt<>::channel_logger_mt;
    boost::log::record open_record()
    {
        // Perform a quick check first
        if (this->core()->get_logging_enabled())
        {
            open_record_lock lock(this->get_threading_model());
            return open_record_unlocked(this);
        }
        return boost::log::record();
    }
};

#else

using ChannelLogger = boost::log::sources::channel_logger_mt<>;

#endif

extern ChannelLogger
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

struct RendezVousData
{
    boost::mutex mtx;
    unsigned int generation{};
    unsigned int count{};
    boost::condition_variable cond;
};

class DecoderIOContext;


// Inspired by http://dranger.com/ffmpeg/ffmpeg.html

class FFmpegDecoder final : public IFrameDecoder, public IAudioPlayerCallback
{
   public:
    FFmpegDecoder(std::unique_ptr<IAudioPlayer> audioPlayer);
    ~FFmpegDecoder() override;

    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    void SetFrameFormat(FrameFormat format, bool allowDirect3dData) override;

    bool openUrls(std::initializer_list<std::string> urls, const std::string& inputFormat = {}, bool useSAN = false) override;
    bool openStream(std::unique_ptr<std::streambuf> stream) override;
    bool seekDuration(int64_t duration);
    bool seekByPercent(double percent) override;

    void videoReset() override;

    double volume() const override;

    bool isPlaying() const override { return m_isPlaying; }
    bool isPaused() const override { return m_isPaused; }

    void setFrameListener(IFrameListener* listener) override { m_frameListener = listener; }

    void setDecoderListener(FrameDecoderListener* listener) override
    {
        m_decoderListener = listener;
    }

    bool getFrameRenderingData(FrameRenderingData* data) override;

    double getDurationSecs(int64_t duration) const override;

    void doOnFinishedDisplayingFrame(unsigned int generation, FinishedDisplayingMode mode) override;

    void close() override;
    void play(bool isPaused = false) override;
    bool pauseResume() override;
    bool nextFrame() override;
    bool prevFrame() override;
    void setVolume(double volume) override;

    int getNumAudioTracks() const override;
    int getAudioTrack() const override;
    void setAudioTrack(int idx) override;

    RationalNumber getSpeedRational() const override;
    void setSpeedRational(const RationalNumber& speed) override;

    bool getHwAccelerated() const override;
    void setHwAccelerated(bool hwAccelerated) override;

    std::vector<std::string> getProperties() const override;

    std::pair<bool, bool> isVideoAudioCompatible() const override;

    std::vector<std::string> listSubtitles() const override;
    bool getSubtitles(int idx, std::function<bool(double, double, const std::string&)> addIntervalCallback) override;

    void setImageConversionFunc(ImageConversionFunc func) override;

   private:
    struct VideoParseContext;

    // Threads
    void parseRunnable(int idx);
    void audioParseRunnable();
    void videoParseRunnable();
    void displayRunnable();

    bool doOpen(const std::initializer_list<std::string>& urls = {});
    void LoadSubtitleItems(const std::initializer_list<std::string>& urls);
    bool dispatchPacket(int idx, AVPacket& packet);
    void handleSubtitlePacket(int idx, const AVPacket& packet);
    void flush(int idx);
    void startAudioThread();
    void startVideoThread();
    bool resetDecoding(int64_t seekDuration, bool resetVideo);
    bool doSeekFrame(int idx, int64_t seekDuration, AVPacket* packet);
    bool respawn(int64_t seekDuration, bool resetVideo);

    void fixDuration();

    bool handleAudioPacket(
        const AVPacket& packet,
        std::vector<uint8_t>& resampleBuffer,
        bool failed, double& scheduledEndTime);
    void setupAudioSwrContext(AVFrame* audioFrame);
    bool handleAudioFrame(
        double frame_clock, uint8_t* write_data, int64_t write_size, bool failed);
    bool handleVideoPacket(
        const AVPacket& packet,
        double& videoClock,
        VideoParseContext& context);
    bool handleVideoFrame(
        AVFramePtr& frame,
        double pts,
        VideoParseContext& context);

    // IAudioPlayerCallback
    void AppendFrameClock(double frame_clock) override;

    void resetVariables();
    void closeProcessing();

    bool resetVideoProcessing();
    bool setupAudioProcessing();
    bool setupAudioCodec();
    bool initAudioOutput();

    void seekWhilePaused();

    void handleDirect3dData(AVFrame* videoFrame, bool forceConversion);

    double GetHiResTime() const;

    bool basedOnVideoStream() const;

    // Frame display listener
    IFrameListener* m_frameListener;

    FrameDecoderListener* m_decoderListener;

    // Indicators
    bool m_isPlaying;

    std::unique_ptr<boost::thread> m_mainVideoThread;
    std::unique_ptr<boost::thread> m_mainAudioThread;
    std::vector<std::unique_ptr<boost::thread>> m_mainParseThreads;
    std::unique_ptr<boost::thread> m_mainDisplayThread;

    // Synchronization
    boost::atomic<double> m_audioPTS;

    // Real duration from video stream
    int64_t m_startTime;
    boost::atomic_int64_t m_currentTime;
    int64_t m_duration;

    boost::atomic_int64_t m_prevTime;

    // Basic stuff
    std::vector<AVFormatContext*> m_formatContexts;

    std::vector<boost::atomic_bool> m_formatContextInterrupts;

    boost::atomic_int64_t m_seekDuration;
    boost::atomic_int64_t m_videoResetDuration;

    RendezVousData m_seekRendezVous;
    RendezVousData m_videoResetRendezVous;

    boost::atomic_bool m_videoResetting;

    // Video Stuff
    enum { VIDEO_START_CLOCK_NOT_INITIALIZED = -1000000000 };
    boost::atomic<double> m_videoStartClock;

    const AVCodec* m_videoCodec;
    AVCodecContext* m_videoCodecContext;
    AVStream* m_videoStream;
    int m_videoContextIndex;
    int m_videoStreamNumber;

    // Audio Stuff
    const AVCodec* m_audioCodec;
    AVCodecContext* m_audioCodecContext;
    AVStream* m_audioStream;
    int m_audioContextIndex;
    boost::atomic<int> m_audioStreamNumber;
    SwrContext* m_audioSwrContext;

    struct AudioParams
    {
        int frequency;
#if LIBAVUTIL_VERSION_MAJOR < 57
        int channels;
        int64_t channel_layout;
        auto num_channels() const { return channels; }
#else
        AVChannelLayout channel_layout;
        auto num_channels() const { return channel_layout.nb_channels; }
#endif
        AVSampleFormat format;

        AudioParams() = default;
        AudioParams(int freq, int chans, AVSampleFormat fmt);
        ~AudioParams();
        AudioParams& operator=(const AudioParams& other);
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
        MAX_VIDEO_QUEUE_SIZE = 100 * 1024 * 1024,
        MAX_AUDIO_QUEUE_SIZE = 15 * 1024 * 1024,
        MAX_VIDEO_FRAMES = 500,
        MAX_AUDIO_FRAMES = 500,
    };
    FQueue<MAX_VIDEO_QUEUE_SIZE, MAX_VIDEO_FRAMES> m_videoPacketsQueue;
    FQueue<MAX_AUDIO_QUEUE_SIZE, MAX_AUDIO_FRAMES> m_audioPacketsQueue;

    VQueue m_videoFramesQueue;

    bool m_frameDisplayingRequested;

    unsigned int m_generation = 0;

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

    std::unique_ptr<DecoderIOContext> m_ioCtx;

    boost::atomic<boost::chrono::high_resolution_clock::duration> m_referenceTime;

    boost::atomic<RationalNumber> m_speedRational; // Numerator, Denominator

    bool m_hwAccelerated;

    struct SubtitleItem {
        int contextIdx;
        int streamIdx;
        std::string description;
        std::string url;
    };

    std::vector<SubtitleItem> m_subtitleItems;

    mutable boost::mutex m_addIntervalMutex;

    int m_subtitleIdx;
    std::function<bool(double, double, const std::string&)> m_addIntervalCallback;

    AVCodecContext* m_subtitlesCodecContext;

    boost::atomic_shared_ptr<ImageConversionFunc> m_imageConversionFunc;
};
