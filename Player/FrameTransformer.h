// FrameTransformer.h
#pragma once

#include "ordered_scoped_token.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <libavutil/frame.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}
#include <string>
#include <vector>

/*
Usage:
    FrameTransformer frameTransformer("crop=iw/2:ih:0:0,split[left][tmp];[tmp]hflip[right];[left][right] hstack");
    auto [width, height] = m_frameDecoder->getVideoSize();
    frameTransformer.init(width, height);
    m_frameDecoder->setImageConversionFunc(frameTransformer);
*/

class FrameTransformer {
public:
    // filter_desc: e.g. "scale=1280:720,format=nv12" or "format=nv12"
    FrameTransformer(const std::string& filter_desc);
    //FrameTransformer(FrameTransformer&& other) = default;
    ~FrameTransformer();

    // Initialize with input properties (called automatically on first process if not called)
    int init(int in_w, int in_h, AVRational time_base = {1, 25});

    // Process one frame. pts is optional (pass AV_NOPTS_VALUE if unknown).
    // Returns 0 on success, negative AVERROR on failure.
    bool operator()(OrderedScopedTokenGenerator::Token t, 
                uint8_t* input, int in_stride, int in_w, int in_h, int64_t pts,
                std::vector<uint8_t>& output, int& out_w, int& out_h);

    void reset();

private:
    std::string filter_desc_;
    //AVFilterGraph* graph_ = nullptr;
    std::shared_ptr<AVFilterGraph> graph_;
    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;
    bool initialized_ = false;
    AVRational time_base_ = {1,25};

    int create_graph(int in_w, int in_h, AVPixelFormat in_pix_fmt);
    void free_graph();
};
