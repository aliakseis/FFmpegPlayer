#include "subtitles.h"

#include "makeguard.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>

namespace {

/*
 *  from mpv/sub/sd_ass.c
 * ass_to_plaintext() was written by wm4 and he says it can be under LGPL
 */
std::string ass_to_plaintext(const char *in)
{
    std::string result;

    bool in_tag = false;
    const char *open_tag_pos = nullptr;
    bool in_drawing = false;
    while (*in) {
        if (in_tag) {
            if (in[0] == '}') {
                in += 1;
                in_tag = false;
            } else if (in[0] == '\\' && in[1] == 'p') {
                in += 2;
                // Skip text between \pN and \p0 tags. A \p without a number
                // is the same as \p0, and leading 0s are also allowed.
                in_drawing = false;
                while (in[0] >= '0' && in[0] <= '9') {
                    if (in[0] != '0')
                        in_drawing = true;
                    in += 1;
                }
            } else {
                in += 1;
            }
        } else {
            if (in[0] == '\\' && (in[1] == 'N' || in[1] == 'n')) {
                in += 2;
                result += '\n';
            } else if (in[0] == '\\' && in[1] == 'h') {
                in += 2;
                result += ' ';
            } else if (in[0] == '{') {
                open_tag_pos = in;
                in += 1;
                in_tag = true;
            } else {
                if (!in_drawing)
                    result += in[0];
                in += 1;
            }
        }
    }
    // A '{' without a closing '}' is always visible.
    if (in_tag) {
        result += open_tag_pos;
    }

    return result;
}

std::string fromAss(const char* ass) {
    const auto b = ass_to_plaintext(ass);
    int hour1, min1, sec1, hunsec1,hour2, min2, sec2, hunsec2;
    char line[1024];
    // fixme: "\0" maybe not allowed
    if (sscanf(b.c_str(), "Dialogue: Marked=%*d,%d:%d:%d.%d,%d:%d:%d.%d%1023[^\r\n]", //&nothing,
                            &hour1, &min1, &sec1, &hunsec1,
                            &hour2, &min2, &sec2, &hunsec2,
                            line) < 9)
        if (sscanf(b.c_str(), "Dialogue: %*d,%d:%d:%d.%d,%d:%d:%d.%d%1023[^\r\n]", //&nothing,
                &hour1, &min1, &sec1, &hunsec1,
                &hour2, &min2, &sec2, &hunsec2,
                line) < 9)
            return b; //libass ASS_Event.Text has no Dialogue
    auto ret = strchr(line, ',');
    if (!ret)
        return line;
    static const char kDefaultStyle[] = "Default,";
    for (int comma = 0; comma < 6; comma++) {
        if (!(ret = strchr(++ret, ','))) {
            // workaround for ffmpeg decoded srt in ass format: "Dialogue: 0,0:42:29.20,0:42:31.08,Default,Chinese\NEnglish.
            if (!(ret = strstr(line, kDefaultStyle))) {
                if (line[0] == ',') //work around for libav-9-
                    return line + 1;
                return line;
            }
            ret += sizeof(kDefaultStyle) - 1 - 1; // tail \0
        }
    }
    ret++;
    int p = strcspn(b.c_str(), "\r\n");
    if (p == b.size()) //not found
        return ret;

    std::string line2 = b.substr(p + 1);
    boost::algorithm::trim(line2);
    if (line2.empty())
        return ret;

    return ret + ("\n" + line2);
}

} // namespace

AVCodecContext* MakeSubtitlesCodecContext(AVCodecParameters* codecpar)
{
    auto codecContext = avcodec_alloc_context3(nullptr);
    if (codecContext == nullptr) {
        return nullptr;
    }

    auto codecContextGuard = MakeGuard(&codecContext, avcodec_free_context);

    if (avcodec_parameters_to_context(codecContext, codecpar) < 0) {
        return nullptr;
    }

    auto codec = avcodec_find_decoder(codecContext->codec_id);
    if (codec == nullptr)
    {
        return nullptr;  // Codec not found
    }

    // Open codec
    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        assert(false && "Error on codec opening");
        return nullptr;  // Could not open codec
    }

    codecContextGuard.release();

    return codecContext;
}

std::string GetSubtitle(AVCodecContext* ctx, AVPacket& packet)
{
    AVSubtitle sub{};

    auto subtitleGuard = MakeGuard(&sub, avsubtitle_free);

    int got_subtitle = 0;
    int ret = avcodec_decode_subtitle2(ctx, &sub, &got_subtitle, &packet);
    if (ret >= 0 && got_subtitle)
    {
        std::string text;

        for (unsigned i = 0; i < sub.num_rects; i++) {
            switch (sub.rects[i]->type) {
            case SUBTITLE_ASS:
                text += fromAss(sub.rects[i]->ass);
                text += '\n';
                break;
            case SUBTITLE_TEXT:
                text += sub.rects[i]->text;
                text += '\n';
                break;
            default:
                break;
            }
        }

        return text;
    }

    return {};
}
