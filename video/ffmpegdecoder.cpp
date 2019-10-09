#include "ffmpegdecoder.h"
#include <limits.h>
#include <stdint.h>

#include "makeguard.h"
#include "interlockedadd.h"

#include <boost/chrono.hpp>
#include <utility>
#include <algorithm>
#include <tuple>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>

#define USE_HWACCEL

// http://stackoverflow.com/questions/34602561
#ifdef USE_HWACCEL
#include "ffmpeg_dxva2.h"
#endif

namespace
{

void FreeVideoCodecContext(AVCodecContext*& videoCodecContext)
{
#ifdef USE_HWACCEL
    if (videoCodecContext)
    {
        if (auto stream = static_cast<InputStream*>(videoCodecContext->opaque))
        {
            avcodec_close(videoCodecContext);
            if (stream->hwaccel_uninit)
                stream->hwaccel_uninit(videoCodecContext);
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
    InputStream* ist = (InputStream*)s->opaque;
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

int ThisThreadInterruptionRequested(void*)
{
    return boost::this_thread::interruption_requested();
}

void log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    if (level <= AV_LOG_ERROR)
    {
        char buffer[4096];
        vsprintf_s(buffer, fmt, vargs);
        auto length = strlen(buffer);
        for (; length > 0 && isspace(buffer[length - 1]); --length)
            ;
        buffer[length] = '\0';
        CHANNEL_LOG(ffmpeg_internal) << buffer;
    }
}

}  // namespace

namespace channel_logger
{

using boost::log::keywords::channel;

boost::log::sources::channel_logger_mt<> 
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
    FILE *fh;

public:
    IOContext(const PathType &datafile);
    ~IOContext();

    void initAVFormatContext(AVFormatContext *);

    bool valid() const { return fh != nullptr; }

    static int IOReadFunc(void *data, uint8_t *buf, int buf_size);
    static int64_t IOSeekFunc(void *data, int64_t pos, int whence);
};

// static
int FFmpegDecoder::IOContext::IOReadFunc(void *data, uint8_t *buf, int buf_size)
{
    IOContext *hctx = (IOContext *)data;
    size_t len = fread(buf, 1, buf_size, hctx->fh);
    if (len == 0)
    {
        // Let FFmpeg know that we have reached EOF, or do something else
        return AVERROR_EOF;
    }
    return (int)len;
}

// whence: SEEK_SET, SEEK_CUR, SEEK_END (like fseek) and AVSEEK_SIZE
// static
int64_t FFmpegDecoder::IOContext::IOSeekFunc(void *data, int64_t pos, int whence)
{
    IOContext *hctx = (IOContext *)data;

    if (whence == AVSEEK_SIZE)
    {
        // return the file size if you wish to
        auto current = _ftelli64(hctx->fh);
        int rs = _fseeki64(hctx->fh, 0, SEEK_END);
        if (rs != 0)
        {
            return -1LL;
        }
        int64_t result = _ftelli64(hctx->fh);
        _fseeki64(hctx->fh, current, SEEK_SET);  // reset to the saved position
        return result;
    }

    int rs = _fseeki64(hctx->fh, pos, whence);
    if (rs != 0)
    {
        return -1LL;
    }
    return _ftelli64(hctx->fh);  // int64_t is usually long long
}

FFmpegDecoder::IOContext::IOContext(const PathType &s)
{
    // allocate buffer
    bufferSize = 1024 * 64;                     // FIXME: not sure what size to use
    buffer = (uint8_t *)av_malloc(bufferSize);  // see destructor for details

                                                // open file
    if (!(fh = 
#ifdef _WIN32
        _wfsopen(s.c_str(), L"rb", _SH_DENYNO)
#else
        _fsopen(s.c_str(), "rb", _SH_DENYNO)
#endif
    ))
    {
        // fprintf(stderr, "MyIOContext: failed to open file %s\n", s.c_str());
        BOOST_LOG_TRIVIAL(error) << "MyIOContext: failed to open file";
    }

    // allocate the AVIOContext
    ioCtx =
        avio_alloc_context(buffer, bufferSize,  // internal buffer and its size
            0,                   // write flag (1=true,0=false)
            (void *)this,  // user data, will be passed to our callback functions
            IOReadFunc,
            0,  // no writing
            IOSeekFunc);
}

FFmpegDecoder::IOContext::~IOContext()
{
    CHANNEL_LOG(ffmpeg_closing) << "In IOContext::~IOContext()";
    if (fh)
        fclose(fh);

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
    size_t len = fread(buffer, 1, bufferSize, fh);
    if (len == 0)
        return;
    _fseeki64(fh, 0, SEEK_SET);  // reset to beginning of file

    AVProbeData probeData = { 0 };
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
      m_audioPlayer(std::move(audioPlayer))
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

    //avdevice_register_all();
    avformat_network_init();
}

FFmpegDecoder::~FFmpegDecoder() { close(); }

void FFmpegDecoder::resetVariables()
{
    m_videoCodec = nullptr;
    m_formatContext = nullptr;
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

    m_generation = 0;

    m_isPaused = false;

    m_seekDuration = AV_NOPTS_VALUE;
    m_videoResetDuration = AV_NOPTS_VALUE;

    m_videoResetting = false;

    m_isVideoSeekingWhilePaused = false;

    m_isPlaying = false;

    m_audioPaused = false;
    m_audioIndices.clear();

    m_speedRational = { 1, 1 };

    CHANNEL_LOG(ffmpeg_closing) << "Variables reset";
}

void FFmpegDecoder::close()
{
    CHANNEL_LOG(ffmpeg_closing) << "Start file closing";

    CHANNEL_LOG(ffmpeg_closing) << "Aborting threads";
    Shutdown(m_mainParseThread);  // controls other threads, hence stop first
    Shutdown(m_mainVideoThread);
    Shutdown(m_mainAudioThread);
    Shutdown(m_mainDisplayThread);

    m_audioPlayer->Close();

    if (m_frameListener)
        m_frameListener->decoderClosing();

    closeProcessing();

    if (m_decoderListener)
        m_decoderListener->playingFinished();
}

void FFmpegDecoder::closeProcessing()
{
    m_audioPacketsQueue.clear();
    m_videoPacketsQueue.clear();

    CHANNEL_LOG(ffmpeg_closing) << "Closing old vars";

    m_mainVideoThread.reset();
    m_mainAudioThread.reset();
    m_mainParseThread.reset();
    m_mainDisplayThread.reset();

    m_audioPlayer->Reset();

    // Free videoFrames
    m_videoFramesQueue.clear();

    sws_freeContext(m_imageCovertContext);

    if (m_audioSwrContext)
    {
        swr_free(&m_audioSwrContext);
    }

    FreeVideoCodecContext(m_videoCodecContext);

    // Close the audio codec
    avcodec_free_context(&m_audioCodecContext);

    bool isFileReallyClosed = false;

    // Close video file
    if (m_formatContext)
    {
        avformat_close_input(&m_formatContext);
        isFileReallyClosed = true;
    }

    m_ioCtx.reset();

    CHANNEL_LOG(ffmpeg_closing) << "Old file closed";

    resetVariables();

    if (m_decoderListener)
        m_decoderListener->decoderClosed(isFileReallyClosed);
}

bool FFmpegDecoder::openFile(const PathType& filename)
{
    return openDecoder(filename, std::string(), true);
}

bool FFmpegDecoder::openUrl(const std::string& url)
{
    return openDecoder(PathType(), url, false);
}

bool FFmpegDecoder::openDecoder(const PathType &file, const std::string& url, bool isFile)
{
    close();

    m_referenceTime = boost::chrono::high_resolution_clock::now().time_since_epoch();

    std::unique_ptr<IOContext> ioCtx;
    if (isFile)
    {
        ioCtx.reset(new IOContext(file));
        if (!ioCtx->valid())
        {
            BOOST_LOG_TRIVIAL(error) << "Couldn't open video/audio file";
            return false;
        }
    }

    AVDictionary *streamOpts = nullptr;
    auto avOptionsGuard = MakeGuard(&streamOpts, av_dict_free);

    m_formatContext = avformat_alloc_context();
    if (isFile)
    {
        ioCtx->initAVFormatContext(m_formatContext);
    }
    else
    {
        av_dict_set(&streamOpts, "stimeout", "5000000", 0); // 5 seconds rtsp timeout.
        av_dict_set(&streamOpts, "rw_timeout", "5000000", 0); // 5 seconds I/O timeout.
        if (boost::starts_with(url, "https://") || boost::starts_with(url, "http://")) // seems to be a bug
        {
            av_dict_set(&streamOpts, "timeout", "5000000", 0); // 5 seconds tcp timeout.
        }
    }

    m_formatContext->interrupt_callback.callback = ThisThreadInterruptionRequested;

    auto formatContextGuard = MakeGuard(&m_formatContext, avformat_close_input);

    // Open video file
    const int error = avformat_open_input(&m_formatContext, url.c_str(), nullptr, &streamOpts);
    if (error != 0)
    {
        BOOST_LOG_TRIVIAL(error) << "Couldn't open video/audio file error: " << error;
        return false;
    }
    CHANNEL_LOG(ffmpeg_opening) << "Opening video/audio file...";

    // Retrieve stream information
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
    {
        CHANNEL_LOG(ffmpeg_opening) << "Couldn't find stream information";
        return false;
    }

    // Find the first video stream
    m_videoStreamNumber = -1;
    m_audioStreamNumber = -1;
    for (unsigned i = m_formatContext->nb_streams; i--;)
    {
        switch (m_formatContext->streams[i]->codecpar->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            m_videoStream = m_formatContext->streams[i];
            m_videoStreamNumber = i;
            break;
        case AVMEDIA_TYPE_AUDIO:
            m_audioIndices.push_back(i);
            m_audioStream = m_formatContext->streams[i];
            m_audioStreamNumber = i;
            break;
        }
    }
    std::reverse(m_audioIndices.begin(), m_audioIndices.end());

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
        : ((m_formatContext->start_time == AV_NOPTS_VALUE)? 0 
            : int64_t((m_formatContext->start_time / av_q2d(timeStream->time_base)) / 1000000LL));
    m_duration = (timeStream->duration > 0)
        ? timeStream->duration
        : ((m_formatContext->duration == AV_NOPTS_VALUE)? 0 
            : int64_t((m_formatContext->duration / av_q2d(timeStream->time_base)) / 1000000LL));

    if (!resetVideoProcessing())
    {
        return false;
    }

    if (!setupAudioProcessing())
    {
        return false;
    }

    formatContextGuard.release();
    m_ioCtx = std::move(ioCtx);

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
        if (!m_videoCodecContext)
            return false;

        auto videoCodecContextGuard = MakeGuard(&m_videoCodecContext, avcodec_free_context);

        if (avcodec_parameters_to_context(m_videoCodecContext, m_videoStream->codecpar) < 0)
            return false;

        m_videoCodec = avcodec_find_decoder(m_videoCodecContext->codec_id);
        if (m_videoCodec == nullptr)
        {
            assert(false && "No such codec found");
            return false;  // Codec not found
        }

#ifdef USE_HWACCEL
        m_videoCodecContext->coded_width = m_videoCodecContext->width;
        m_videoCodecContext->coded_height = m_videoCodecContext->height;

        m_videoCodecContext->thread_count = 1;  // Multithreading is apparently not compatible with hardware decoding
        InputStream *ist = new InputStream();
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
#else
        m_videoCodecContext->thread_count = 2;
        m_videoCodecContext->flags2 |= CODEC_FLAG2_FAST;
#endif


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

    if (m_audioStreamNumber >= 0)
    {
        CHANNEL_LOG(ffmpeg_opening) << "Audio stream number: " << m_audioStreamNumber;
        m_audioCodecContext = avcodec_alloc_context3(nullptr);
        if (!m_audioCodecContext)
            return false;

        auto audioCodecContextGuard = MakeGuard(&m_audioCodecContext, avcodec_free_context);

        if (!setupAudioCodec())
            return false;

        if (!initAudioOutput())
            return false;

        audioCodecContextGuard.release();
    }

    return true;
}

bool FFmpegDecoder::setupAudioCodec()
{
    if (avcodec_parameters_to_context(m_audioCodecContext, m_audioStream->codecpar) < 0)
        return false;

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

    if (!m_mainParseThread)
    {
        m_isPlaying = true;
        m_mainParseThread.reset(new boost::thread(&FFmpegDecoder::parseRunnable, this));
        m_mainDisplayThread.reset(new boost::thread(&FFmpegDecoder::displayRunnable, this));
        CHANNEL_LOG(ffmpeg_opening) << "Playing";
    }
}

void FFmpegDecoder::AppendFrameClock(double frame_clock)
{
    if (!m_mainVideoThread && m_decoderListener && m_seekDuration == AV_NOPTS_VALUE)
    {
        m_decoderListener->changedFramePosition(
            m_startTime,
            int64_t((m_audioPTS + frame_clock) / av_q2d(m_audioStream->time_base)), 
            m_duration + m_startTime);
    }

    int speedNumerator, speedDenominator;
    std::tie(speedNumerator, speedDenominator) = static_cast<const std::pair<int, int>&>(m_speedRational);
    InterlockedAdd(m_audioPTS, frame_clock * speedNumerator / speedDenominator);
}

void FFmpegDecoder::setVolume(double volume)
{
    if (volume < 0 || volume > 1.)
    {
        return;
    }

    CHANNEL_LOG(ffmpeg_volume) << "Volume: " << volume;

    m_audioPlayer->SetVolume(volume);

    if (m_decoderListener)
        m_decoderListener->volumeChanged(volume);
}

double FFmpegDecoder::volume() const { return m_audioPlayer->GetVolume(); }

void FFmpegDecoder::SetFrameFormat(FrameFormat format, bool allowDirect3dData)
{ 
    static_assert(PIX_FMT_YUV420P == AV_PIX_FMT_YUV420P, "FrameFormat and AVPixelFormat values must coincide.");
    static_assert(PIX_FMT_YUYV422 == AV_PIX_FMT_YUYV422, "FrameFormat and AVPixelFormat values must coincide.");
    static_assert(PIX_FMT_RGB24 == AV_PIX_FMT_RGB24,     "FrameFormat and AVPixelFormat values must coincide.");

    m_pixelFormat = (AVPixelFormat)format;
    m_allowDirect3dData = allowDirect3dData;
}

void FFmpegDecoder::finishedDisplayingFrame(unsigned int generation)
{
    {
        boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
        if (generation == m_generation && m_videoFramesQueue.canPop())
        {
            VideoFrame &current_frame = m_videoFramesQueue.front();
            if (current_frame.m_image->format == AV_PIX_FMT_DXVA2_VLD)
            {
                av_frame_unref(current_frame.m_image.get());
            }

            m_videoFramesQueue.popFront();
        }
        m_frameDisplayingRequested = false;
    }
    m_videoFramesCV.notify_all();
}

bool FFmpegDecoder::seekDuration(int64_t duration)
{
    if (m_mainParseThread && m_seekDuration.exchange(duration) == AV_NOPTS_VALUE)
    {
        m_videoPacketsQueue.notify();
        m_audioPacketsQueue.notify();
    }

    return true;
}

void FFmpegDecoder::videoReset()
{
    m_videoResetting = true;
    if (m_mainParseThread && m_videoResetDuration.exchange(m_currentTime) == AV_NOPTS_VALUE)
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
        if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
            InterlockedAdd(m_videoStartClock, GetHiResTime() - m_pauseTimer);
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
    if (!m_frameDisplayingRequested || m_mainParseThread == nullptr || m_videoResetting)
    {
        return false;
    }

    VideoFrame &current_frame = m_videoFramesQueue.front();
    if (!current_frame.m_image->data)
    {
        return false;
    }
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
    else
    {
        data->aspectNum = 1;
        data->aspectDen = 1;
    }

#ifdef USE_HWACCEL
    if (current_frame.m_image->format == AV_PIX_FMT_DXVA2_VLD)
    {
        data->d3d9device = get_device(m_videoCodecContext);
        data->surface = (IDirect3DSurface9**)&current_frame.m_image->data[3];
    }
#endif

    return true;
}

bool FFmpegDecoder::pauseResume()
{
    if (m_mainParseThread == nullptr)
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
        if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
        InterlockedAdd(m_videoStartClock, GetHiResTime() - m_pauseTimer);
        m_isPaused = false;
    }
    m_isPausedCV.notify_all();

    return true;
}

bool FFmpegDecoder::nextFrame()
{
    if (m_mainParseThread == nullptr)
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
        if (m_videoStartClock != VIDEO_START_CLOCK_NOT_INITIALIZED)
            InterlockedAdd(m_videoStartClock, currentTime - m_pauseTimer);
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
    const int audioStreamNumber = m_audioStreamNumber;
    return (audioStreamNumber < 0)
        ? -1 
        : std::find(m_audioIndices.begin(), m_audioIndices.end(), audioStreamNumber) - m_audioIndices.begin();
}

void FFmpegDecoder::setAudioTrack(int idx)
{
    if (idx >= 0 && idx < m_audioIndices.size())
        m_audioStreamNumber = m_audioIndices[idx];
}

std::pair<int, int> FFmpegDecoder::getSpeedRational() const
{
    return m_speedRational;
}

void FFmpegDecoder::setSpeedRational(const std::pair<int, int>& speed)
{
    boost::lock_guard<boost::mutex> locker(m_isPausedMutex);

    const auto time = GetHiResTime();
    m_speedRational = speed;
    int speedNumerator, speedDenominator;
    std::tie(speedNumerator, speedDenominator) = static_cast<const std::pair<int, int>&>(speed);

    m_referenceTime = (boost::chrono::high_resolution_clock::now()
            - boost::chrono::microseconds(int64_t(time * speedDenominator / speedNumerator * 1000000.)))
        .time_since_epoch();
}


void FFmpegDecoder::handleDirect3dData(AVFrame* videoFrame)
{
#ifdef USE_HWACCEL
    if (!m_allowDirect3dData && videoFrame->format == AV_PIX_FMT_DXVA2_VLD)
    {
        dxva2_retrieve_data_call(m_videoCodecContext, videoFrame);
        assert(videoFrame->format != AV_PIX_FMT_DXVA2_VLD);
    }
#endif
}

double FFmpegDecoder::GetHiResTime()
{
    int speedNumerator, speedDenominator;
    std::tie(speedNumerator, speedDenominator) = static_cast<const std::pair<int, int>&>(m_speedRational);
    return boost::chrono::duration_cast<boost::chrono::microseconds>(
        boost::chrono::high_resolution_clock::now() - boost::chrono::high_resolution_clock::time_point(m_referenceTime)).count()
            / 1000000. * speedNumerator / speedDenominator;
}
