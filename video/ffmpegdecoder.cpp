#include "ffmpegdecoder.h"

#include <climits>
#include <cstdint>

#include "makeguard.h"
#include "interlockedadd.h"
#include "subtitles.h"

#include <boost/chrono.hpp>
#include <memory>
#include <utility>
#include <algorithm>
#include <tuple>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>


extern "C"
{
#include "libavutil/pixdesc.h"
#include "libavdevice/avdevice.h"
}

#ifdef _WIN32
#define USE_HWACCEL
#endif

// http://stackoverflow.com/questions/34602561
#ifdef USE_HWACCEL
#include "ffmpeg_dxva2.h"
#endif

namespace
{

void FreeVideoCodecContext(AVCodecContext*& videoCodecContext)
{
#ifdef USE_HWACCEL
    if (videoCodecContext != nullptr)
    {
        if (auto stream = static_cast<InputStream*>(videoCodecContext->opaque))
        {
            avcodec_close(videoCodecContext);
            if (stream->hwaccel_uninit != nullptr) {
                stream->hwaccel_uninit(videoCodecContext);
            }
            delete stream;
        }
        videoCodecContext->opaque = nullptr;
    }
#endif

    // Close the codec
    avcodec_free_context(&videoCodecContext);
}

#ifdef USE_HWACCEL
AVPixelFormat GetHwFormat(AVCodecContext *s, const AVPixelFormat *pix_fmts)
{
    auto* ist = static_cast<InputStream*>(s->opaque);
    ist->active_hwaccel_id = HWACCEL_DXVA2;
    ist->hwaccel_pix_fmt = AV_PIX_FMT_DXVA2_VLD;
    return ist->hwaccel_pix_fmt;
}
#endif

inline void Shutdown(const std::unique_ptr<boost::thread>& th)
{
    if (th)
    {
        th->interrupt();
        th->join();
    }
}

int ThisThreadInterruptionRequested(void* /*unused*/)
{
    return static_cast<int>(boost::this_thread::interruption_requested());
}

void log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    if (level <= AV_LOG_ERROR)
    {
        char buffer[4096];
        int length = vsnprintf(buffer, sizeof(buffer), fmt, vargs);
        if (length > 0)
        {
            for (; length > 0 && (isspace(static_cast<unsigned char>(buffer[length - 1])) != 0); --length) {
                ;
            }
            buffer[length] = '\0';
            CHANNEL_LOG(ffmpeg_internal) << buffer;
        }
    }
}

}  // namespace

namespace channel_logger
{

using boost::log::keywords::channel;

ChannelLogger
    ffmpeg_closing(channel = "ffmpeg_closing"),
    ffmpeg_opening(channel = "ffmpeg_opening"),
    ffmpeg_pause(channel = "ffmpeg_pause"),
    ffmpeg_seek(channel = "ffmpeg_seek"),
    ffmpeg_sync(channel = "ffmpeg_sync"),
    ffmpeg_threads(channel = "ffmpeg_threads"),
    ffmpeg_volume(channel = "ffmpeg_volume"),
    ffmpeg_internal(channel = "ffmpeg_internal");

} // namespace channel_logger

std::unique_ptr<IFrameDecoder> GetFrameDecoder(std::unique_ptr<IAudioPlayer> audioPlayer)
{
    return std::unique_ptr<IFrameDecoder>(new FFmpegDecoder(std::move(audioPlayer)));
}

// https://gist.github.com/xlphs/9895065
class FFmpegDecoder::IOContext
{
private:
    AVIOContext *ioCtx;
    uint8_t *buffer;  // internal buffer for ffmpeg
    int bufferSize;
    std::unique_ptr<std::streambuf> stream;

public:
    IOContext(std::unique_ptr<std::streambuf> s);
    ~IOContext();

    void initAVFormatContext(AVFormatContext * /*pCtx*/);

    static int IOReadFunc(void *data, uint8_t *buf, int buf_size);
    static int64_t IOSeekFunc(void *data, int64_t pos, int whence);
};

// static
int FFmpegDecoder::IOContext::IOReadFunc(void *data, uint8_t *buf, int buf_size)
{
    auto *hctx = static_cast<IOContext *>(data);
    auto len = hctx->stream->sgetn((char*)buf, buf_size);
    if (len <= 0)
    {
        // Let FFmpeg know that we have reached EOF, or do something else
        return AVERROR_EOF;
    }
    return static_cast<int>(len);
}

// whence: SEEK_SET, SEEK_CUR, SEEK_END (like fseek) and AVSEEK_SIZE
// static
int64_t FFmpegDecoder::IOContext::IOSeekFunc(void *data, int64_t pos, int whence)
{
    auto *hctx = static_cast<IOContext *>(data);

    if (whence == AVSEEK_SIZE)
    {
        // return the file size if you wish to
        auto current = hctx->stream->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
        auto result = hctx->stream->pubseekoff(0, std::ios_base::end, std::ios_base::in);
        hctx->stream->pubseekoff(current, std::ios_base::beg, std::ios_base::in);
        return result;
    }

    std::ios_base::seekdir dir;
    switch (whence)
    {
    case SEEK_SET: dir = std::ios_base::beg; break;
    case SEEK_CUR: dir = std::ios_base::cur; break;
    case SEEK_END: dir = std::ios_base::end; break;
    default: return -1LL;
    }

    return hctx->stream->pubseekoff(pos, dir);
}

FFmpegDecoder::IOContext::IOContext(std::unique_ptr<std::streambuf> s)
    : stream(std::move(s))
{
    // allocate buffer
    bufferSize = 1024 * 64;                     // FIXME: not sure what size to use
    buffer = static_cast<uint8_t *>(av_malloc(bufferSize));  // see destructor for details

    // allocate the AVIOContext
    ioCtx =
        avio_alloc_context(buffer, bufferSize,  // internal buffer and its size
            0,                   // write flag (1=true,0=false)
            (void *)this,  // user data, will be passed to our callback functions
            IOReadFunc,
            nullptr,  // no writing
            IOSeekFunc);
}

FFmpegDecoder::IOContext::~IOContext()
{
    CHANNEL_LOG(ffmpeg_closing) << "In IOContext::~IOContext()";

    // NOTE: ffmpeg messes up the buffer
    // so free the buffer first then free the context
    av_free(ioCtx->buffer);
    ioCtx->buffer = nullptr;
    av_free(ioCtx);
}

void FFmpegDecoder::IOContext::initAVFormatContext(AVFormatContext *pCtx)
{
    pCtx->pb = ioCtx;
    pCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    // you can specify a format directly
    // pCtx->iformat = av_find_input_format("h264");

    // or read some of the file and let ffmpeg do the guessing
    auto len = stream->sgetn((char*)buffer, bufferSize);
    if (len <= 0) {
        return;
    }
    // reset to beginning of file
    stream->pubseekoff(0, std::ios_base::beg, std::ios_base::in);

    AVProbeData probeData = { nullptr };
    probeData.buf = buffer;
    probeData.buf_size = bufferSize - 1;
    probeData.filename = "";
    pCtx->iformat = av_probe_input_format(&probeData, 1);
}

//////////////////////////////////////////////////////////////////////////////

FFmpegDecoder::FFmpegDecoder(std::unique_ptr<IAudioPlayer> audioPlayer)
    : m_frameListener(nullptr),
      m_decoderListener(nullptr),
      m_audioSettings({48000, 2, av_get_default_channel_layout(2), AV_SAMPLE_FMT_S16}),
      m_pixelFormat(AV_PIX_FMT_YUV420P),
      m_allowDirect3dData(false),
      m_audioPlayer(std::move(audioPlayer)),
      m_hwAccelerated(true)
{
    av_log_set_level(AV_LOG_ERROR);
    av_log_set_callback(log_callback);

    m_audioPlayer->SetCallback(this);

    resetVariables();

    // init codecs
#if ( LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(58,9,100) )
    avcodec_register_all();
    av_register_all();
#endif

    avdevice_register_all();

    avformat_network_init();
}

FFmpegDecoder::~FFmpegDecoder() { close(); }

void FFmpegDecoder::resetVariables()
{
    m_videoCodec = nullptr;
    m_formatContexts.clear();
    m_videoCodecContext = nullptr;
    m_audioCodecContext = nullptr;
    m_audioSwrContext = nullptr;
    m_videoStream = nullptr;
    m_audioStream = nullptr;

    m_startTime = 0;
    m_currentTime = 0;
    m_duration = 0;

    m_imageCovertContext = nullptr;

    m_audioPTS = 0;

    m_frameDisplayingRequested = false;

    ++m_generation;

    m_isPaused = false;

    m_seekDuration = AV_NOPTS_VALUE;
    m_videoResetDuration = AV_NOPTS_VALUE;

    m_seekRendezVous.count = 0;
    m_videoResetRendezVous.count = 0;

    m_videoResetting = false;

    m_videoStartClock = VIDEO_START_CLOCK_NOT_INITIALIZED;

    m_isVideoSeekingWhilePaused = false;

    m_isPlaying = false;

    m_audioPaused = false;
    m_audioIndices.clear();

    m_subtitleItems.clear();

    m_subtitleIdx = -1;
    m_addIntervalCallback = {};

    m_subtitlesCodecContext = nullptr;

    m_speedRational = { 1, 1 };

    CHANNEL_LOG(ffmpeg_closing) << "Variables reset";
}

void FFmpegDecoder::close()
{
    CHANNEL_LOG(ffmpeg_closing) << "Start file closing";

    CHANNEL_LOG(ffmpeg_closing) << "Aborting threads";
    // controls other threads, hence stop first
    for (auto& mainParseThread : m_mainParseThreads)
    {
        Shutdown(mainParseThread);
    }
    Shutdown(m_mainVideoThread);
    Shutdown(m_mainAudioThread);
    Shutdown(m_mainDisplayThread);

    m_audioPlayer->Close();

    if (m_frameListener != nullptr) {
        m_frameListener->decoderClosing();
    }

    closeProcessing();

    if (m_decoderListener != nullptr) {
        m_decoderListener->playingFinished();
    }
}

void FFmpegDecoder::closeProcessing()
{
    m_audioPacketsQueue.clear();
    m_videoPacketsQueue.clear();

    CHANNEL_LOG(ffmpeg_closing) << "Closing old vars";

    m_mainVideoThread.reset();
    m_mainAudioThread.reset();
    m_mainParseThreads.clear();
    m_mainDisplayThread.reset();

    // Free videoFrames
    {
        boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
        m_videoFramesQueue.clear();
    }

    sws_freeContext(m_imageCovertContext);

    if (m_audioSwrContext != nullptr)
    {
        swr_free(&m_audioSwrContext);
    }

    FreeVideoCodecContext(m_videoCodecContext);

    // Close the audio codec
    avcodec_free_context(&m_audioCodecContext);

    avcodec_free_context(&m_subtitlesCodecContext);

    bool isFileReallyClosed = false;

    // Close video file
    for (auto& formatContext : m_formatContexts)
        if (formatContext != nullptr)
        {
            avformat_close_input(&formatContext);
            isFileReallyClosed = true;
        }

    m_ioCtx.reset();

    CHANNEL_LOG(ffmpeg_closing) << "Old file closed";

    resetVariables();

    if (m_decoderListener != nullptr) {
        m_decoderListener->decoderClosed(isFileReallyClosed);
    }
}


bool FFmpegDecoder::openUrls(std::initializer_list<std::string> urls, const std::string& inputFormat)
{
    close();

    for (const auto& url : urls)
    {
        auto formatContext = avformat_alloc_context();

        AVDictionary *streamOpts = nullptr;
        auto avOptionsGuard = MakeGuard(&streamOpts, av_dict_free);

        av_dict_set(&streamOpts, "stimeout", "5000000", 0); // 5 seconds rtsp timeout.
        av_dict_set(&streamOpts, "rw_timeout", "5000000", 0); // 5 seconds I/O timeout.
        if (boost::starts_with(url, "https://") || boost::starts_with(url, "http://")) // seems to be a bug
        {
            av_dict_set(&streamOpts, "timeout", "5000000", 0); // 5 seconds tcp timeout.
        }

        formatContext->interrupt_callback.callback = ThisThreadInterruptionRequested;

        auto formatContextGuard = MakeGuard(&formatContext, avformat_close_input);

        // Open video file
        auto iformat = inputFormat.empty() ? nullptr : av_find_input_format(inputFormat.c_str());
        if (iformat)
        {
            av_dict_set(&streamOpts, "rtbufsize", "15000000", 0); // https://superuser.com/questions/1158820/ffmpeg-real-time-buffer-issue-rtbufsize-parameter
        }
        const int error = avformat_open_input(&formatContext, url.c_str(), iformat, &streamOpts);
        if (error != 0)
        {
            BOOST_LOG_TRIVIAL(error) << "Couldn't open video/audio file error: " << error;
            return false;
        }
        CHANNEL_LOG(ffmpeg_opening) << "Opening video/audio file...";

        // Retrieve stream information
        if (avformat_find_stream_info(formatContext, nullptr) < 0)
        {
            CHANNEL_LOG(ffmpeg_opening) << "Couldn't find stream information";
            return false;
        }

        m_formatContexts.push_back(formatContext);
        formatContextGuard.release();
    }

    return doOpen(urls);
}

bool FFmpegDecoder::openStream(std::unique_ptr<std::streambuf> stream)
{
    close();

    auto ioCtx = std::make_unique<IOContext>(std::move(stream));

    auto formatContext = avformat_alloc_context();

    auto formatContextGuard = MakeGuard(&formatContext, avformat_close_input);

    ioCtx->initAVFormatContext(formatContext);

    formatContext->interrupt_callback.callback = ThisThreadInterruptionRequested;

    AVDictionary *streamOpts = nullptr;
    auto avOptionsGuard = MakeGuard(&streamOpts, av_dict_free);

    const int error = avformat_open_input(&formatContext, nullptr, nullptr, &streamOpts);
    if (error != 0)
    {
        BOOST_LOG_TRIVIAL(error) << "Couldn't open video/audio file error: " << error;
        return false;
    }
    CHANNEL_LOG(ffmpeg_opening) << "Opening video/audio file...";

    // Retrieve stream information
    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        CHANNEL_LOG(ffmpeg_opening) << "Couldn't find stream information";
        return false;
    }

    m_formatContexts.push_back(formatContext);
    formatContextGuard.release();
    m_ioCtx = std::move(ioCtx);

    return doOpen();
}

bool FFmpegDecoder::doOpen(const std::initializer_list<std::string>& urls)
{
    m_referenceTime = boost::chrono::high_resolution_clock::now().time_since_epoch();

    // Find the first video stream
    m_videoContextIndex = -1;
    m_videoStreamNumber = -1;
    m_audioContextIndex = -1;
    m_audioStreamNumber = -1;
    for (int contextIdx = m_formatContexts.size(); --contextIdx >= 0;)
    {
        const auto formatContext = m_formatContexts[contextIdx];
        for (int i = formatContext->nb_streams; --i >= 0;)
        {
            switch (formatContext->streams[i]->codecpar->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
                m_videoStream = formatContext->streams[i];
                m_videoContextIndex = contextIdx;
                m_videoStreamNumber = i;
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (m_audioContextIndex == -1 || m_audioContextIndex == contextIdx)
                {
                    m_audioIndices.push_back(i);
                    m_audioStream = formatContext->streams[i];
                    m_audioContextIndex = contextIdx;
                    m_audioStreamNumber = i;
                }
                break;
            }
        }
    }
    std::reverse(m_audioIndices.begin(), m_audioIndices.end());

    int lastSubtitleNr = 0;
    for (int contextIdx = 0; contextIdx < m_formatContexts.size(); ++contextIdx)
    {
        const auto formatContext = m_formatContexts[contextIdx];
        for (int i = 0; i < formatContext->nb_streams; ++i)
        {
            if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                std::string description;
                if (auto title = av_dict_get(formatContext->streams[i]->metadata, "title", nullptr, 0))
                {
                    description = title->value;
                }
                else
                {
                    description = "Track " + std::to_string(++lastSubtitleNr);
                    if (auto lang = av_dict_get(formatContext->streams[i]->metadata, "language", nullptr, 0))
                    {
                        description += " (";
                        description += lang->value;
                        description += ')';
                    }
                }
                m_subtitleItems.push_back({ contextIdx, i, std::move(description), 
                    (urls.size() == 0) ? std::string() : *(urls.begin() + contextIdx) });
            }
        }
    }

    AVStream* timeStream = nullptr;

    if (m_videoStreamNumber == -1)
    {
        CHANNEL_LOG(ffmpeg_opening) << "Can't find video stream";
    }
    else
    {
        timeStream = m_videoStream;
    }

    if (m_audioStreamNumber == -1)
    {
        CHANNEL_LOG(ffmpeg_opening) << "No audio stream";
        if (m_videoStreamNumber == -1)
        {
            return false; // no multimedia
        }
    }
    else if (m_videoStreamNumber == -1)
    {
        // Changing video -> audio duration
        timeStream = m_audioStream;
    }

    m_startTime = (timeStream->start_time > 0)
        ? timeStream->start_time
        : ((m_formatContexts[0]->start_time == AV_NOPTS_VALUE)? 0 
            : int64_t((m_formatContexts[0]->start_time / av_q2d(timeStream->time_base)) / AV_TIME_BASE));
    m_duration = (timeStream->duration > 0)
        ? timeStream->duration
        : ((m_formatContexts[0]->duration == AV_NOPTS_VALUE)? 0
            : int64_t((m_formatContexts[0]->duration / av_q2d(timeStream->time_base)) / AV_TIME_BASE));

    if (!resetVideoProcessing())
    {
        return false;
    }

    if (!setupAudioProcessing())
    {
        return false;
    }


    return true;
}

bool FFmpegDecoder::resetVideoProcessing()
{
    FreeVideoCodecContext(m_videoCodecContext);

    // Find the decoder for the video stream
    if (m_videoStreamNumber >= 0)
    {
        CHANNEL_LOG(ffmpeg_opening) << "Video stream number: " << m_videoStreamNumber;
        m_videoCodecContext = avcodec_alloc_context3(nullptr);
        if (m_videoCodecContext == nullptr) {
            return false;
        }

        auto videoCodecContextGuard = MakeGuard(&m_videoCodecContext, avcodec_free_context);

        if (avcodec_parameters_to_context(m_videoCodecContext, m_videoStream->codecpar) < 0) {
            return false;
        }

        m_videoCodec = avcodec_find_decoder(m_videoCodecContext->codec_id);
        if (m_videoCodec == nullptr)
        {
            assert(false && "No such codec found");
            return false;  // Codec not found
        }

#ifdef USE_HWACCEL
        if (m_hwAccelerated)
        {
            m_videoCodecContext->coded_width = m_videoCodecContext->width;
            m_videoCodecContext->coded_height = m_videoCodecContext->height;

            m_videoCodecContext->thread_count = 1;  // Multithreading is apparently not compatible with hardware decoding
            auto *ist = new InputStream();
            ist->hwaccel_id = HWACCEL_AUTO;
            ist->dec = m_videoCodec;
            ist->dec_ctx = m_videoCodecContext;

            m_videoCodecContext->opaque = ist;
            if (dxva2_init(m_videoCodecContext) >= 0)
            {
                m_videoCodecContext->get_buffer2 = ist->hwaccel_get_buffer;
                m_videoCodecContext->get_format = GetHwFormat;
                m_videoCodecContext->thread_safe_callbacks = 1;
            }
            else
            {
                delete ist;
                m_videoCodecContext->opaque = nullptr;

                m_videoCodecContext->thread_count = 2;
                m_videoCodecContext->flags2 |= AV_CODEC_FLAG2_FAST;
            }
        }
        else
#endif
        {
            m_videoCodecContext->thread_count = 2;
            m_videoCodecContext->flags2 |= AV_CODEC_FLAG2_FAST;
        }


    // Open codec
        if (avcodec_open2(m_videoCodecContext, m_videoCodec, nullptr) < 0)
        {
            assert(false && "Error on codec opening");
            return false;  // Could not open codec
        }

        // Some broken files can pass codec check but don't have width x height
        if (m_videoCodecContext->width <= 0 || m_videoCodecContext->height <= 0)
        {
            assert(false && "This file lacks resolution");
            return false;  // Could not open codec
        }

        videoCodecContextGuard.release();
    }

    return true;
}

bool FFmpegDecoder::setupAudioProcessing()
{
    m_audioCurrentPref = m_audioSettings;

    const auto audioStreamNumber = m_audioStreamNumber.load();
    if (audioStreamNumber >= 0)
    {
        CHANNEL_LOG(ffmpeg_opening) << "Audio stream number: " << audioStreamNumber;
        m_audioCodecContext = avcodec_alloc_context3(nullptr);
        if (m_audioCodecContext == nullptr) {
            return false;
        }

        auto audioCodecContextGuard = MakeGuard(&m_audioCodecContext, avcodec_free_context);

        if (!setupAudioCodec()) {
            return false;
        }

        if (!initAudioOutput()) {
            return false;
        }

        audioCodecContextGuard.release();
    }

    return true;
}

bool FFmpegDecoder::setupAudioCodec()
{
    if (avcodec_parameters_to_context(m_audioCodecContext, m_audioStream->codecpar) < 0) {
        return false;
    }

    // Find audio codec
    m_audioCodec = avcodec_find_decoder(m_audioCodecContext->codec_id);
    if (m_audioCodec == nullptr)
    {
        assert(false && "No such codec found");
        return false;  // Codec not found
    }

    // Open audio codec
    if (avcodec_open2(m_audioCodecContext, m_audioCodec, nullptr) < 0)
    {
        assert(false && "Error on codec opening");
        return false;  // Could not open codec
    }

    return true;
}

bool FFmpegDecoder::initAudioOutput()
{
    return m_audioPlayer->Open(av_get_bytes_per_sample(m_audioSettings.format),
        m_audioSettings.channels, &m_audioSettings.frequency);
}

void FFmpegDecoder::play(bool isPaused)
{
    CHANNEL_LOG(ffmpeg_opening) << "Starting playing";

    m_isPaused = isPaused;

    if (isPaused)
    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
        m_pauseTimer = GetHiResTime();
    }

    if (m_mainParseThreads.empty())
    {
        m_isPlaying = true;
        for (int i = 0; i < m_formatContexts.size(); ++i)
        {
            m_mainParseThreads.push_back(std::make_unique<boost::thread>(&FFmpegDecoder::parseRunnable, this, i));
        }
        m_mainDisplayThread = std::make_unique<boost::thread>(&FFmpegDecoder::displayRunnable, this);
        CHANNEL_LOG(ffmpeg_opening) << "Playing";
    }
}

void FFmpegDecoder::AppendFrameClock(double frame_clock)
{
    if (!m_mainVideoThread && (m_decoderListener != nullptr) && m_seekDuration == AV_NOPTS_VALUE)
    {
        m_decoderListener->changedFramePosition(
            m_startTime,
            int64_t((m_audioPTS + frame_clock) / av_q2d(m_audioStream->time_base)), 
            m_duration + m_startTime);
    }

    const auto speed = getSpeedRational();
    InterlockedAdd(m_audioPTS, frame_clock * speed.numerator / speed.denominator);
}

void FFmpegDecoder::setVolume(double volume)
{
    if (volume < 0 || volume > 1.)
    {
        return;
    }

    CHANNEL_LOG(ffmpeg_volume) << "Volume: " << volume;

    m_audioPlayer->SetVolume(volume);

    if (m_decoderListener != nullptr) {
        m_decoderListener->volumeChanged(volume);
    }
}

double FFmpegDecoder::volume() const { return m_audioPlayer->GetVolume(); }

void FFmpegDecoder::SetFrameFormat(FrameFormat format, bool allowDirect3dData)
{ 
    static_assert(PIX_FMT_YUV420P == AV_PIX_FMT_YUV420P, "FrameFormat and AVPixelFormat values must coincide.");
    static_assert(PIX_FMT_YUYV422 == AV_PIX_FMT_YUYV422, "FrameFormat and AVPixelFormat values must coincide.");
    static_assert(PIX_FMT_RGB24 == AV_PIX_FMT_RGB24,     "FrameFormat and AVPixelFormat values must coincide.");
    static_assert(PIX_FMT_BGR24 == AV_PIX_FMT_BGR24,     "FrameFormat and AVPixelFormat values must coincide.");

    m_pixelFormat = static_cast<AVPixelFormat>(format);
    m_allowDirect3dData = allowDirect3dData;
}

void FFmpegDecoder::finishedDisplayingFrame(unsigned int generation)
{
    {
        boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
        if (generation != m_generation || !m_videoFramesQueue.canPop())
        {
            return;
        }
        VideoFrame &current_frame = m_videoFramesQueue.front();
        if (current_frame.m_image->format == AV_PIX_FMT_DXVA2_VLD)
        {
            av_frame_unref(current_frame.m_image.get());
        }

        m_videoFramesQueue.popFront();
        m_frameDisplayingRequested = false;
    }
    m_videoFramesCV.notify_all();
}

bool FFmpegDecoder::seekDuration(int64_t duration)
{
    if (!m_mainParseThreads.empty() && m_seekDuration.exchange(duration) == AV_NOPTS_VALUE)
    {
        m_videoPacketsQueue.notify();
        m_audioPacketsQueue.notify();
    }

    return true;
}

void FFmpegDecoder::videoReset()
{
    m_videoResetting = true;
    if (!m_mainParseThreads.empty() && m_videoResetDuration.exchange(m_currentTime) == AV_NOPTS_VALUE)
    {
        m_videoPacketsQueue.notify();
        m_audioPacketsQueue.notify();
    }
}

void FFmpegDecoder::seekWhilePaused()
{
    boost::lock_guard<boost::mutex> locker(m_isPausedMutex);

    const bool paused = m_isPaused;
    if (paused)
    {
        if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED) {
            InterlockedAdd(m_videoStartClock, GetHiResTime() - m_pauseTimer);
        }
        m_pauseTimer = GetHiResTime();
    }

    m_isVideoSeekingWhilePaused = paused;
}

bool FFmpegDecoder::seekByPercent(double percent)
{
    return seekDuration(m_startTime + int64_t(m_duration * percent));
}

bool FFmpegDecoder::getFrameRenderingData(FrameRenderingData *data)
{
    if (!m_frameDisplayingRequested || m_mainParseThreads.empty() || m_videoResetting)
    {
        return false;
    }

    VideoFrame &current_frame = m_videoFramesQueue.front();
    if (current_frame.m_image == nullptr)
    {
        return false;
    }
#ifdef USE_HWACCEL
    if (current_frame.m_image->format == AV_PIX_FMT_DXVA2_VLD)
    {
        data->d3d9device = get_device(m_videoCodecContext);
        auto surface = static_cast<IDirect3DSurface9*>(static_cast<void*>(current_frame.m_image->data[3]));
        if (surface == nullptr)
        {
            return false;
        }
        data->surface = surface;
    }
#endif

    data->image = current_frame.m_image->data;
    data->pitch = current_frame.m_image->linesize;
    data->width = current_frame.m_image->width;
    data->height = current_frame.m_image->height;
    if (current_frame.m_image->sample_aspect_ratio.num != 0
        && current_frame.m_image->sample_aspect_ratio.den != 0)
    {
        data->aspectNum = current_frame.m_image->sample_aspect_ratio.num;
        data->aspectDen = current_frame.m_image->sample_aspect_ratio.den;
    }
    else if (m_videoStream && m_videoStream->sample_aspect_ratio.num != 0
        && m_videoStream->sample_aspect_ratio.den != 0)
    {
        data->aspectNum = m_videoStream->sample_aspect_ratio.num;
        data->aspectDen = m_videoStream->sample_aspect_ratio.den;
    }
    else
    {
        data->aspectNum = 1;
        data->aspectDen = 1;
    }

    return true;
}

bool FFmpegDecoder::pauseResume()
{
    if (m_mainParseThreads.empty())
    {
        return false;
    }

    if (!m_isPaused)
    {
        CHANNEL_LOG(ffmpeg_pause) << "Pause";
        {
            boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
            m_isPaused = true;
            m_pauseTimer = GetHiResTime();
        }
        m_videoFramesCV.notify_all();
        m_videoPacketsQueue.notify();
        m_audioPacketsQueue.notify();

        return true;
    }

    CHANNEL_LOG(ffmpeg_pause) << "Resume";
    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
        if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED) {
            InterlockedAdd(m_videoStartClock, GetHiResTime() - m_pauseTimer);
        }
        m_isVideoSeekingWhilePaused = false;
        m_isPaused = false;
    }
    m_isPausedCV.notify_all();

    return true;
}

bool FFmpegDecoder::nextFrame()
{
    if (m_mainParseThreads.empty())
    {
        return false;
    }

    if (m_videoPacketsQueue.empty())
    {
        return false;
    }

    CHANNEL_LOG(ffmpeg_pause) << "Next frame";
    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);

        if (!m_isPaused || m_isVideoSeekingWhilePaused)
        {
            return false;
        }
 
        const auto currentTime = GetHiResTime();
        if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED) {
            InterlockedAdd(m_videoStartClock, currentTime - m_pauseTimer);
        }
        m_pauseTimer = currentTime;

        m_isVideoSeekingWhilePaused = true;
    }
    m_isPausedCV.notify_all();
    m_videoPacketsQueue.notify();
    return true;
}

int FFmpegDecoder::getNumAudioTracks() const
{
    return m_audioIndices.size();
}

int FFmpegDecoder::getAudioTrack() const
{
    const auto audioStreamNumber = m_audioStreamNumber.load();
    return (audioStreamNumber < 0)
        ? -1
        : std::find(m_audioIndices.begin(), m_audioIndices.end(), audioStreamNumber) - m_audioIndices.begin();
}

void FFmpegDecoder::setAudioTrack(int idx)
{
    if (idx >= 0 && idx < m_audioIndices.size()) {
        m_audioStreamNumber = m_audioIndices[idx];
    }
}

RationalNumber FFmpegDecoder::getSpeedRational() const
{
    return m_speedRational;
}

void FFmpegDecoder::setSpeedRational(const RationalNumber& speed)
{
    boost::lock_guard<boost::mutex> locker(m_isPausedMutex);

    const auto time = GetHiResTime();
    m_speedRational = speed;

    m_referenceTime = (boost::chrono::high_resolution_clock::now()
            - boost::chrono::microseconds(int64_t(time * speed.denominator / speed.numerator * 1000000.)))
        .time_since_epoch();
}

std::vector<std::string> FFmpegDecoder::getProperties() const
{
    std::vector<std::string> result;

    for (auto formatContext : m_formatContexts)
    {
        if (formatContext && formatContext->iformat && formatContext->iformat->long_name)
            result.emplace_back(formatContext->iformat->long_name);
    }

    if (m_videoCodec && m_videoCodec->long_name)
        result.emplace_back(m_videoCodec->long_name);

    if (m_videoStream && m_videoCodecContext)
    {
        const auto eps_zero = 0.000025;

        double fps = 0;
        if (m_videoStream->r_frame_rate.den)
            fps = av_q2d(m_videoStream->r_frame_rate);

        if (fps < eps_zero && m_videoStream->avg_frame_rate.den)
            fps = av_q2d(m_videoStream->avg_frame_rate);

        if (fps < eps_zero && m_videoStream->time_base.num && m_videoStream->time_base.den)
            fps = 1.0 / av_q2d(m_videoStream->time_base);

        int bpp = 0;
        int depth = 0;
        if (auto av_pix_fmt_desc = av_pix_fmt_desc_get(
            (m_videoCodecContext->sw_pix_fmt == AV_PIX_FMT_NONE)
                ? m_videoCodecContext->pix_fmt : m_videoCodecContext->sw_pix_fmt))
        {
            bpp = av_get_bits_per_pixel(av_pix_fmt_desc);
            for (int i = 0; i < av_pix_fmt_desc->nb_components; ++i)
                if (av_pix_fmt_desc->comp[i].depth > depth)
                    depth = av_pix_fmt_desc->comp[i].depth;
        }

        char buffer[1000];
        snprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
            "%d / %d @ %.2f FPS %d BPP %d bits", 
            m_videoCodecContext->width, m_videoCodecContext->height, fps, bpp, depth);
        result.emplace_back(buffer);
    }

    if (m_audioCodec && m_audioCodec->long_name)
        result.emplace_back(m_audioCodec->long_name);

    return result;
}


void FFmpegDecoder::handleDirect3dData(AVFrame* videoFrame, bool forceConversion)
{
#ifdef USE_HWACCEL
    if ((forceConversion || !m_allowDirect3dData) && videoFrame->format == AV_PIX_FMT_DXVA2_VLD)
    {
        dxva2_retrieve_data(m_videoCodecContext, videoFrame);
        assert(videoFrame->format != AV_PIX_FMT_DXVA2_VLD);
    }
#endif
}

double FFmpegDecoder::GetHiResTime()
{
    const auto speed = getSpeedRational();
    return boost::chrono::duration_cast<boost::chrono::microseconds>(
        boost::chrono::high_resolution_clock::now() - boost::chrono::high_resolution_clock::time_point(m_referenceTime)).count()
            / 1000000. * speed.numerator / speed.denominator;
}

bool FFmpegDecoder::getHwAccelerated() const
{
    return m_hwAccelerated;
}

void FFmpegDecoder::setHwAccelerated(bool hwAccelerated)
{
    m_hwAccelerated = hwAccelerated;
}

std::vector<std::string> FFmpegDecoder::listSubtitles() const
{
    std::vector<std::string> result;
    for (const auto& item : m_subtitleItems)
    {
        result.push_back(item.description);
    }

    return result;
}

bool FFmpegDecoder::getSubtitles(int idx, std::function<bool(double, double, const std::string&)> addIntervalCallback)
{
    if (idx < 0 || idx >= m_subtitleItems.size())
        return false;

    {
        boost::lock_guard<boost::mutex> locker(m_addIntervalMutex);
        m_subtitleIdx = idx;
        m_addIntervalCallback = addIntervalCallback;

        avcodec_free_context(&m_subtitlesCodecContext);
    }

    const auto& subtitleItem = m_subtitleItems.at(idx);
    if (subtitleItem.url.empty())
    { 
        return true;
    }

    auto formatContext = avformat_alloc_context();
    if (formatContext == nullptr) {
        return false;
    }

    auto formatContextGuard = MakeGuard(&formatContext, avformat_close_input);

    const auto streamNumber = subtitleItem.streamIdx;
    int error = avformat_open_input(&formatContext, subtitleItem.url.c_str(), nullptr, nullptr);
    if (error != 0)
    {
        return false;
    }

    auto codecContext = MakeSubtitlesCodecContext(formatContext->streams[streamNumber]->codecpar);
    if (codecContext == nullptr) {
        return false;
    }

    auto codecContextGuard = MakeGuard(&codecContext, avcodec_free_context);

    bool ok = false;
    AVPacket packet;
    while (av_read_frame(formatContext, &packet) >= 0)
    {
        if (packet.stream_index == streamNumber)
        {
            std::string text = GetSubtitle(codecContext, packet);
            if (!text.empty())
            {
                if (!addIntervalCallback(packet.pts / 1000., (packet.pts + packet.duration) / 1000., text))
                {
                    av_packet_unref(&packet);
                    return false;
                }
                ok = true;
            }
        }
        av_packet_unref(&packet);
    }

    return ok;
}

void FFmpegDecoder::setImageConversionFunc(ImageConversionFunc func)
{
    m_imageConversionFunc = boost::make_shared<ImageConversionFunc>(std::move(func));
}
