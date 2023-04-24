#include "ffmpegdecoder.h"
#include "makeguard.h"
#include "interlockedadd.h"

extern "C"
{
#include "libavutil/imgutils.h"
}

#include <boost/log/trivial.hpp>
#include <tuple>

namespace {

#ifdef _MSC_VER

// Use scale to convert lsb formats to msb, depending how many bits there are:
// 32768 = 9 bits
// 16384 = 10 bits
// 4096 = 12 bits
// 256 = 16 bits
void Convert16To8Row_SSSE3(const __m128i* src_y,
                           __m128i* dst_y,
                           int scale,
                           int width) {
    /*
  asm volatile (
    "movd      %3,%%xmm2                      \n"
    "punpcklwd %%xmm2,%%xmm2                  \n"
    "pshufd    $0x0,%%xmm2,%%xmm2             \n"

    // 32 pixels per loop.
    LABELALIGN
    "1:                                       \n"
    "movdqu    (%0),%%xmm0                    \n"
    "movdqu    0x10(%0),%%xmm1                \n"
    "add       $0x20,%0                       \n"
    "pmulhuw   %%xmm2,%%xmm0                  \n"
    "pmulhuw   %%xmm2,%%xmm1                  \n"
    "packuswb  %%xmm1,%%xmm0                  \n"
    "movdqu    %%xmm0,(%1)                    \n"
    "add       $0x10,%1                       \n"
    "sub       $0x10,%2                       \n"
    "jg        1b                             \n"
  : "+r"(src_y),   // %0
    "+r"(dst_y),   // %1
    "+r"(width)    // %2
  : "r"(scale)     // %3
  : "memory", "cc", "xmm0", "xmm1", "xmm2");
  */

    __m128i s = _mm_cvtsi32_si128(scale);
    s = _mm_unpacklo_epi16(s, s);
    s = _mm_shuffle_epi32(s, 0);

    for (; width > 0; width -= 16)
    {
        __m128i a0 = *src_y++;
        __m128i a1 = *src_y++;

        a0 = _mm_mulhi_epu16(a0, s);
        a1 = _mm_mulhi_epu16(a1, s);

        a0 = _mm_packus_epi16(a0, a1);

        *dst_y++ = a0;
    }
}

void Convert16To8Row_Any_SSSE3(const uint16_t* src_ptr, uint8_t* dst_ptr, int scale, int width) 
{
    enum {
          SBPP = 2, 
          BPP = 1,
          MASK = 15,
    };

    const int r = width & MASK;                            
    const int n = width & ~MASK;
    if (n > 0) {                                     
        Convert16To8Row_SSSE3((const __m128i*)src_ptr, (__m128i*)dst_ptr, scale, n);
    }     
    if (r > 0) {
        __declspec(align(16)) uint16_t temp[32];
        __declspec(align(16)) uint8_t out[32];
        memset(temp, 0, 32 * SBPP); /* for msan */
        memcpy(temp, src_ptr + n, r * SBPP);
        Convert16To8Row_SSSE3((const __m128i*)temp, (__m128i*)out, scale, MASK + 1);
        memcpy(dst_ptr + n, out, r * BPP);
    }
}

void Convert16To8Plane(const uint16_t* src_y,
                       int src_stride_y,
                       uint8_t* dst_y,
                       int dst_stride_y,
                       int scale,  // 16384 for 10 bits
                       int width,
                       int height) 
{
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_y = dst_y + (height - 1) * dst_stride_y;
    dst_stride_y = -dst_stride_y;
  }
  // Coalesce rows.
  if (src_stride_y == width && dst_stride_y == width) {
    width *= height;
    height = 1;
    src_stride_y = dst_stride_y = 0;
  }

  // Convert plane
  for (int y = 0; y < height; ++y) {
      Convert16To8Row_Any_SSSE3(src_y, dst_y, scale, width);
    src_y += src_stride_y;
    dst_y += dst_stride_y;
  }
}

int I010ToI420(const uint16_t* src_y,
               int src_stride_y,
               const uint16_t* src_u,
               int src_stride_u,
               const uint16_t* src_v,
               int src_stride_v,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_u,
               int dst_stride_u,
               uint8_t* dst_v,
               int dst_stride_v,
               int width,
               int height)
{
  int halfwidth = (width + 1) >> 1;
  int halfheight = (height + 1) >> 1;
  if (!src_u || !src_v || !dst_u || !dst_v || width <= 0 || height == 0) {
    return -1;
  }
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    halfheight = (height + 1) >> 1;
    src_y = src_y + (height - 1) * src_stride_y;
    src_u = src_u + (halfheight - 1) * src_stride_u;
    src_v = src_v + (halfheight - 1) * src_stride_v;
    src_stride_y = -src_stride_y;
    src_stride_u = -src_stride_u;
    src_stride_v = -src_stride_v;
  }

  // Convert Y plane.
  Convert16To8Plane(src_y, src_stride_y, dst_y, dst_stride_y, 16384, width,
                    height);
  // Convert UV planes.
  Convert16To8Plane(src_u, src_stride_u, dst_u, dst_stride_u, 16384, halfwidth,
                    halfheight);
  Convert16To8Plane(src_v, src_stride_v, dst_v, dst_stride_v, 16384, halfwidth,
                    halfheight);
  return 0;
}

#endif

bool frameToImage(
    VideoFrame& videoFrameData,
    AVFramePtr& m_videoFrame,
    SwsContext*& m_imageCovertContext,
    AVPixelFormat m_pixelFormat)
{
    if (m_videoFrame->format == m_pixelFormat
        || m_videoFrame->format == AV_PIX_FMT_DXVA2_VLD)
    {
        std::swap(m_videoFrame, videoFrameData.m_image);
    }
    else
    {
        const int width = m_videoFrame->width;
        const int height = m_videoFrame->height;

        videoFrameData.realloc(m_pixelFormat, width, height);

#ifdef _MSC_VER
        if (m_videoFrame->format == AV_PIX_FMT_YUV420P10LE && m_pixelFormat == AV_PIX_FMT_YUV420P
            && !((intptr_t(m_videoFrame->data[0]) & 15) || (m_videoFrame->linesize[0] & 15)
            || (intptr_t(m_videoFrame->data[1]) & 15) || (m_videoFrame->linesize[1] & 15)
            || (intptr_t(m_videoFrame->data[2]) & 15) || (m_videoFrame->linesize[2] & 15)))
        {
            I010ToI420(
                (const uint16_t*)m_videoFrame->data[0],
                m_videoFrame->linesize[0] / 2,
                (const uint16_t*)m_videoFrame->data[1],
                m_videoFrame->linesize[1] / 2,
                (const uint16_t*)m_videoFrame->data[2],
                m_videoFrame->linesize[2] / 2,
                videoFrameData.m_image->data[0],
                videoFrameData.m_image->linesize[0],
                videoFrameData.m_image->data[1],
                videoFrameData.m_image->linesize[1],
                videoFrameData.m_image->data[2],
                videoFrameData.m_image->linesize[2],
                width, height
                );
        }
        else
#endif
        {
            // Prepare image conversion
            m_imageCovertContext =
                sws_getCachedContext(m_imageCovertContext, m_videoFrame->width, m_videoFrame->height,
                    static_cast<AVPixelFormat>(m_videoFrame->format), width, height, m_pixelFormat,
                    0, nullptr, nullptr, nullptr);

            assert(m_imageCovertContext != nullptr);

            if (m_imageCovertContext == nullptr)
            {
                return false;
            }

            // Doing conversion
            if (sws_scale(m_imageCovertContext, m_videoFrame->data, m_videoFrame->linesize, 0,
                m_videoFrame->height, videoFrameData.m_image->data, videoFrameData.m_image->linesize) <= 0)
            {
                assert(false && "sws_scale failed");
                BOOST_LOG_TRIVIAL(error) << "sws_scale failed";
                return false;
            }
        }

        videoFrameData.m_image->sample_aspect_ratio = m_videoFrame->sample_aspect_ratio;
    }

    return true;
}

auto GetAsyncConversionFunction(AVFramePtr input,
    std::promise<VideoFrame *> &videoFramePromise,
    boost::shared_ptr<IFrameDecoder::ImageConversionFunc> imageConversionFunc,
    AVPixelFormat pixelFormat)
{
    return [input = std::move(input),
        outputFut = videoFramePromise.get_future(),
        imageConversionFunc = std::move(imageConversionFunc),
        pixelFormat]() mutable
    {
        try {
            const int stride = (input->width + 1) & ~1;

            std::vector<uint8_t> img(stride * (input->height + (input->height + 1) / 2));

            const auto data = img.data();

            if (input->format == AV_PIX_FMT_NV12)
            {
                av_image_copy_plane(data, stride, input->data[0], input->linesize[0], input->width, input->height);
                av_image_copy_plane(data + stride * input->height, stride, input->data[1], input->linesize[1], input->width, input->height / 2);
            }
            else
            {
                auto img_convert_ctx = sws_getContext(
                    input->width,
                    input->height,
                    (AVPixelFormat)input->format,
                    input->width,
                    input->height,
                    AV_PIX_FMT_NV12,
                    SWS_FAST_BILINEAR, NULL, NULL, NULL);

                uint8_t* const dst[] = { data, data + stride * input->height };
                const int dstStride[] = { stride, stride };

                sws_scale(img_convert_ctx, input->data, input->linesize, 0, input->height,
                    dst, dstStride);

                sws_freeContext(img_convert_ctx);
            }

            std::vector<uint8_t> outputImg;

            int outputHeight{};
            int outputWidth{};

            (*imageConversionFunc)(data, stride, input->width, input->height, outputImg, outputWidth, outputHeight);

            const int outputStride = outputWidth;

            auto output = outputFut.get();
            if (!output)
                return false;

            output->realloc(pixelFormat, outputWidth, outputHeight);

            auto img_convert_ctx = sws_getContext(
                outputWidth,
                outputHeight,
                AV_PIX_FMT_NV12,
                outputWidth,
                outputHeight,
                pixelFormat,
                SWS_FAST_BILINEAR, NULL, NULL, NULL);

            const auto outputData = outputImg.data();

            uint8_t* const src[] = { outputData, outputData + outputStride * outputHeight };
            const int srcStride[] = { outputStride, outputStride };

            sws_scale(img_convert_ctx,
                src, srcStride,
                0, outputHeight,
                output->m_image->data,
                output->m_image->linesize);

            sws_freeContext(img_convert_ctx);
        }
        catch (const std::exception& ex) {
            CHANNEL_LOG(ffmpeg_sync) << "Exception in async converter: " << typeid(ex).name() << ": " << ex.what();
            return false;
        }

        return true;
    };
}

} // namespace

struct FFmpegDecoder::VideoParseContext
{
    bool initialized = false;
    int numSkipped = 0;
};

void FFmpegDecoder::videoParseRunnable()
{
    CHANNEL_LOG(ffmpeg_threads) << "Video thread started";
    m_videoStartClock = VIDEO_START_CLOCK_NOT_INITIALIZED;
    double videoClock = 0; // pts of last decoded frame / predicted pts of next decoded frame

    VideoParseContext context{};

    while (!boost::this_thread::interruption_requested())
    {
        AVPacket packet;
        if (!m_videoPacketsQueue.pop(packet))
        {
            break;
        }

        auto packetGuard = MakeGuard(&packet, av_packet_unref);

        handleVideoPacket(packet, videoClock, context);
    }
}

bool FFmpegDecoder::handleVideoPacket(
    const AVPacket& packet,
    double& videoClock,
    VideoParseContext& context)
{
    const int ret = avcodec_send_packet(m_videoCodecContext, &packet);
    if (ret < 0) {
        return false;
    }

    AVFramePtr videoFrame(av_frame_alloc());
    while (avcodec_receive_frame(m_videoCodecContext, videoFrame.get()) == 0)
    {
		const int64_t duration_stamp = videoFrame->best_effort_timestamp;

        // compute the exact PTS for the picture if it is omitted in the stream
        // pts1 is the dts of the pkt / pts of the frame
        if (duration_stamp != AV_NOPTS_VALUE)
        {
            videoClock = duration_stamp * av_q2d(m_videoStream->time_base);
        }
        const double pts = videoClock;

        // update video clock for next frame
        // for MPEG2, the frame can be repeated, so we update the clock accordingly
        const double frameDelay = av_q2d(m_videoCodecContext->time_base) *
            (1. + videoFrame->repeat_pict * 0.5);
        videoClock += frameDelay;

        if (!handleVideoFrame(videoFrame, pts, context)) {
            break;
        }
    }

    return true;
}

bool FFmpegDecoder::handleVideoFrame(
    AVFramePtr& videoFrame,
    double pts,
    VideoParseContext& context)
{
    enum { MAX_SKIPPED_TILL_REDRAW = 5 };
    enum { SKIP_LOOP_FILTER_THRESHOLD = 50 };
    const double MAX_DELAY = 0.2;

    const int64_t duration_stamp = videoFrame->best_effort_timestamp;

    boost::posix_time::time_duration td(boost::posix_time::pos_infin);
    bool inNextFrame = false;

    {
        boost::unique_lock<boost::mutex> locker(m_isPausedMutex);
        while (m_isPaused && !m_isVideoSeekingWhilePaused)
        {
            m_isPausedCV.wait(locker);
        }

        const bool isPaused = m_isPaused;
        inNextFrame = isPaused && m_isVideoSeekingWhilePaused;
        if (!context.initialized || inNextFrame)
        {
            const auto val = (isPaused ? m_pauseTimer : GetHiResTime()) - pts;
            m_videoStartClock = val;
            CHANNEL_LOG(ffmpeg_sync) << "isPaused = " << isPaused << " m_videoStartClock = " << val;
        }

        // Skipping frames
        if (context.initialized && !inNextFrame && !m_videoPacketsQueue.empty())
        {
            const double deltaTime = m_videoStartClock + pts - GetHiResTime();
            if (deltaTime <= 0)
            {
                if (deltaTime < -MAX_DELAY)
                {
                    InterlockedAdd(m_videoStartClock, MAX_DELAY);
                }

                ++context.numSkipped;
                if (context.numSkipped == SKIP_LOOP_FILTER_THRESHOLD)
                {
                    if (m_videoCodecContext->skip_loop_filter != AVDISCARD_ALL)
                    {
                        // https://trac.kodi.tv/ticket/4943
                        CHANNEL_LOG(ffmpeg_sync) << "skip_loop_filter = AVDISCARD_ALL";
                        m_videoCodecContext->skip_loop_filter = AVDISCARD_ALL;
                    }
                }
                if ((context.numSkipped % MAX_SKIPPED_TILL_REDRAW) != 0)
                {
                    CHANNEL_LOG(ffmpeg_sync) << "Hard skip frame";

                    // pause
                    return !(isPaused && !m_isVideoSeekingWhilePaused);
                }
            }
            else
            {
                const auto speed = getSpeedRational();
                context.numSkipped = 0;
                td = boost::posix_time::milliseconds(
                    int(deltaTime * 1000.  * speed.denominator / speed.numerator) + 1);
            }
        }
    }

    context.initialized = true;

    boost::shared_ptr<ImageConversionFunc> imageConversionFunc = m_imageConversionFunc;
    const bool useAsyncConversion = imageConversionFunc != nullptr && (*imageConversionFunc);

    handleDirect3dData(videoFrame.get(), useAsyncConversion);

    std::future<bool> convert;
    std::promise<VideoFrame*> videoFramePromise;

    auto promGuard = MakeGuard(&videoFramePromise, [](auto promise) { promise->set_value(nullptr); });

    if (useAsyncConversion)
    {
        AVFramePtr input(av_frame_alloc());

        std::swap(input, videoFrame);

        auto asyncConversion = GetAsyncConversionFunction(std::move(input),
            videoFramePromise,
            std::move(imageConversionFunc),
            m_pixelFormat);

        convert = std::async(std::launch::async, std::move(asyncConversion));
    }

    {
        boost::unique_lock<boost::mutex> locker(m_videoFramesMutex);

        if (!m_videoFramesCV.timed_wait(locker, td, [this]
        {
            return m_isPaused && !m_isVideoSeekingWhilePaused ||
                m_videoFramesQueue.canPush();
        }))
        {
            CHANNEL_LOG(ffmpeg_sync) << "Frame wait abandoned";
            return true;
        }
    }

    {
        boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
        if (m_isPaused && !m_isVideoSeekingWhilePaused)
        {
            return false;
        }

        m_isVideoSeekingWhilePaused = false;
    }

    if (inNextFrame)
    {
        m_isPausedCV.notify_all();

        if (m_prevTime != AV_NOPTS_VALUE)
        {
            const int64_t duration_stamp = videoFrame->best_effort_timestamp;
            if (duration_stamp != AV_NOPTS_VALUE && duration_stamp < m_prevTime)
            {
                boost::lock_guard<boost::mutex> locker(m_isPausedMutex);
                if (m_isPaused)
                {
                    m_isVideoSeekingWhilePaused = true;
                    return true;
                }
            }

            m_prevTime = AV_NOPTS_VALUE;
        }
    }

    VideoFrame& current_frame = m_videoFramesQueue.back();

    if (!useAsyncConversion && !frameToImage(current_frame, videoFrame, m_imageCovertContext, m_pixelFormat))
    {
        return true;
    }

    current_frame.m_pts = pts;
    current_frame.m_duration = duration_stamp;

    if (useAsyncConversion)
    {
        promGuard.release();
        videoFramePromise.set_value(&current_frame);
        current_frame.m_convert = std::move(convert);
    }

    {
        boost::lock_guard<boost::mutex> locker(m_videoFramesMutex);
        m_videoFramesQueue.pushBack();
    }
    m_videoFramesCV.notify_all();

    return true;
}
