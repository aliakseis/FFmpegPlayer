// FrameTransformer.cpp (implementation - trimmed for clarity)
#include "stdafx.h"

#include "FrameTransformer.h"
#include <stdexcept>
#include <cstring>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

const auto PIX_FMT = AV_PIX_FMT_NV12;

FrameTransformer::FrameTransformer(std::string filter_desc)
  : filter_desc_(std::move(filter_desc)) {}

int FrameTransformer::init(int in_w, int in_h, AVRational time_base){
    time_base_ = time_base;
    return create_graph(in_w, in_h, PIX_FMT);
}

int FrameTransformer::create_graph(int in_w, int in_h, AVPixelFormat in_pix_fmt){
    if (initialized_) return 0;
    int ret = 0;
    char args[512];

    {
        auto graph = avfilter_graph_alloc();
        if (!graph)
            return AVERROR(ENOMEM);
        graph_.reset(graph, [](AVFilterGraph* p) { avfilter_graph_free(&p); });
    }

    const AVFilter* buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) { ret = AVERROR_FILTER_NOT_FOUND; goto fail; }

    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             in_w, in_h, in_pix_fmt, time_base_.num, time_base_.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in", args, nullptr, graph_.get());
    if (ret < 0) goto fail;

    ret = avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out", nullptr, nullptr, graph_.get());
    if (ret < 0) goto fail;

    // Set buffersink to accept NV12 output
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_NV12, AV_PIX_FMT_NONE };
    ret = av_opt_set_int_list(buffersink_ctx_, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) goto fail;

    // Parse and link the user filter chain between src and sink
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs  = avfilter_inout_alloc();
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = buffersink_ctx_;
    inputs->pad_idx     = 0;
    inputs->next        = nullptr;

    ret = avfilter_graph_parse_ptr(graph_.get(), filter_desc_.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) goto fail;

    ret = avfilter_graph_config(graph_.get(), nullptr);
    if (ret < 0) goto fail;

    w_ = in_w;
    h_ = in_h;
    initialized_ = true;
    return 0;

fail:
    free_graph();
    return ret;
}

bool FrameTransformer::operator()(OrderedScopedTokenGenerator::Token t, uint8_t* input, int in_stride, int in_w, int in_h, int64_t pts,
                              std::vector<uint8_t>& output, int& out_w, int& out_h)
{
    if (w_ != in_w || h_ != in_h)
        free_graph();

    if (!initialized_) {
        int r = init(in_w, in_h, time_base_); // assume input pixfmt; caller can init explicitly
        if (r < 0)
            return false;// r;
    }

    AVFrame* frame = av_frame_alloc();
    frame->format = PIX_FMT; // adjust if needed
    frame->width  = in_w;
    frame->height = in_h;
    frame->linesize[0] = in_stride;
    frame->pts    = pts;
    av_image_fill_arrays(frame->data, frame->linesize, input, (AVPixelFormat)frame->format, in_w, in_h, 1);
    
    AVFrame* filt = nullptr;
    {
        auto scope = t.lock(); // blocks until it's this token's generation order

        int ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        av_frame_free(&frame);
        if (ret < 0)
            return false;// ret;

        // pull filtered frames
        filt = av_frame_alloc();
        ret = av_buffersink_get_frame(buffersink_ctx_, filt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&filt);
            return false;// 0;
        }
        if (ret < 0) {
            av_frame_free(&filt);
            return false;// ret;
        }
    }

    // copy NV12 planar data into output vector
    out_w = filt->width; out_h = filt->height;
    int y_size = out_w * out_h;
    int uv_size = out_w * ((out_h+1)/2);
    output.resize(y_size + uv_size);
    // Y plane
    for (int i=0;i<out_h;i++)
        memcpy(output.data() + i*out_w, filt->data[0] + i*filt->linesize[0], out_w);
    // UV interleaved plane
    for (int i=0;i<(out_h+1)/2;i++)
        memcpy(output.data() + y_size + i*out_w, filt->data[1] + i*filt->linesize[1], out_w);

    av_frame_free(&filt);
    return true;// 0;
}

void FrameTransformer::free_graph()
{
    graph_.reset();
    buffersrc_ctx_ = nullptr;
    buffersink_ctx_ = nullptr;
    initialized_ = false;
    w_ = 0;
    h_ = 0;
}
