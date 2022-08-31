#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <string>

AVCodecContext* MakeSubtitlesCodecContext(AVCodecParameters* codecpar);
std::string GetSubtitle(AVCodecContext* ctx, AVPacket& packet);
