#include "stdafx.h"

#include "OpenSubtitlesFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

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

CString ChangePathExtension(const TCHAR* videoPathName, const TCHAR* ext)
{
    CString subRipPathName(videoPathName);
    PathRemoveExtension(subRipPathName.GetBuffer());
    subRipPathName.ReleaseBuffer();
    subRipPathName += ext;
    return subRipPathName;
}

// https://chromium.googlesource.com/chromium/src/third_party/+/refs/heads/master/unrar/src/unicode.cpp
// Source data can be both with and without UTF-8 BOM.
bool IsTextUtf8(const char *Src, size_t SrcSize)
{
    while (SrcSize-- > 0)
    {
        int C = *(Src++);
        int HighOne = 0; // Number of leftmost '1' bits.
        for (int Mask = 0x80; Mask != 0 && (C & Mask) != 0; Mask >>= 1)
            HighOne++;
        if (HighOne == 1 || HighOne > 4)
            return false;
        while (--HighOne > 0)
            if (SrcSize-- == 0 || (*(Src++) & 0xc0) != 0x80)
                return false;
    }
    return true;
}

bool IsTextUtf8(const std::string& Src)
{
    return IsTextUtf8(Src.c_str(), Src.length());
}


bool OpenSubRipFile(std::istream& s,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback)
{
    bool autoDetectedUnicode = true;
    bool ok = false;
    std::string buffer;
    while (std::getline(s, buffer))
    {
        if (std::find_if(buffer.begin(), buffer.end(), [](unsigned char c) { return !std::isspace(c); })
            == buffer.end())
        {
            continue;
        }

        if (!std::getline(s, buffer))
            break;

        int startHr, startMin, startSec, startMsec;
        int endHr, endMin, endSec, endMsec;

        if (sscanf_s(
            buffer.c_str(),
            "%d:%d:%d,%d --> %d:%d:%d,%d",
            &startHr, &startMin, &startSec, &startMsec,
            &endHr, &endMin, &endSec, &endMsec) != 8)
        {
            break;
        }

        const double start = startHr * 3600 + startMin * 60 + startSec + startMsec / 1000.;
        const double end = endHr * 3600 + endMin * 60 + endSec + endMsec / 1000.;

        std::string subtitle;
        while (std::getline(s, buffer) && !buffer.empty())
        {
            subtitle += buffer;
            subtitle += '\n'; // The last '\n' is for aggregating overlapped subtitles (if any)
        }

        if (!unicodeSubtitles && autoDetectedUnicode)
            autoDetectedUnicode = IsTextUtf8(subtitle);

        if (!subtitle.empty())
        {
            addIntervalCallback(start, end, subtitle);
            ok = true;
        }
    }

    if (!unicodeSubtitles)
        unicodeSubtitles = autoDetectedUnicode;

    return ok;
}

bool OpenSubStationAlphaFile(std::istream& s,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback)
{
    bool autoDetectedUnicode = true;
    bool ok = false;
    std::string buffer;
    while (std::getline(s, buffer))
    {
        std::istringstream ss(buffer);

        std::getline(ss, buffer, ':');

        if (buffer != "Dialogue")
            continue;

        double start, end;
        bool skip = false;
        for (int i = 0; i < 9; ++i) // TODO indices from Format?
        {
            std::getline(ss, buffer, ',');
            if (i == 1 || i == 2)
            {
                int hr, min, sec;
                char msecString[10]{};
                if (sscanf_s(
                    buffer.c_str(),
                    "%d:%d:%d.%9s",
                    &hr, &min, &sec, msecString, (unsigned)_countof(msecString)) != 4)
                {
                    return true;
                }

                double msec = 0;
                if (const auto msecStringLen = strlen(msecString))
                {
                    msec = atoi(msecString) / pow(10, msecStringLen);
                }

                ((i == 1) ? start : end)
                    = hr * 3600 + min * 60 + sec + msec;
            }
            else if (i == 3 && buffer == "OP_kar")
            {
                skip = true;
                break;
            }
        }

        if (skip)
            continue;

        std::getline(ss, buffer, {});
        std::string subtitle = ass_to_plaintext(buffer.c_str());

        if (!unicodeSubtitles && autoDetectedUnicode)
            autoDetectedUnicode = IsTextUtf8(subtitle);

        if (!subtitle.empty())
        {
            subtitle += '\n'; // The last '\n' is for aggregating overlapped subtitles (if any)
            addIntervalCallback(start, end, subtitle);
            ok = true;
        }
    }

    if (!unicodeSubtitles)
        unicodeSubtitles = autoDetectedUnicode;

    return ok;
}

template<char C>
class FilterBuf : public std::streambuf
{
    char readBuf_;
    const char* pExternBuf_;

    int_type underflow() override
    {
        if (gptr() == &readBuf_)
            return traits_type::to_int_type(readBuf_);

        for (; *pExternBuf_ == C; ++pExternBuf_);

        if (*pExternBuf_ == '\0')
            return traits_type::eof();

        int_type nextChar = *pExternBuf_++;

        readBuf_ = traits_type::to_char_type(nextChar);

        setg(&readBuf_, &readBuf_, &readBuf_ + 1);
        return traits_type::to_int_type(readBuf_);
    }

public:
    explicit FilterBuf(const char* pExternBuf)
        : pExternBuf_(pExternBuf)
    {
        setg(nullptr, nullptr, nullptr);
    }
};

bool OpenSubFile(
    bool(*doOpenSubFile)(std::istream&, bool&, AddIntervalCallback),
    const TCHAR* videoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback)
{
    do
    {
        std::ifstream s(videoPathName);
        if (!s)
            return false;

        unicodeSubtitles = false;
        const char ch = s.peek();
        if (ch == char(0xFF))
        {
            s.get();
            if (char(s.peek()) == char(0xFE))
            {
                break; // go to unicode part
            }
            s.unget();
        }
        else if (ch == char(0xEF))
        {
            s.get();
            if (char(s.peek()) == char(0xBB))
            {
                s.get();
                if (char(s.peek()) == char(0xBF))
                {
                    unicodeSubtitles = true;
                }
                else
                {
                    s.unget();
                    s.unget();
                }
            }
            else
                s.unget();
        }

        return doOpenSubFile(s, unicodeSubtitles, addIntervalCallback);

    } while (false);

    std::ifstream s(videoPathName, std::ios::binary);
    if (!s)
        return false;

    s.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(s.tellg()) - 2;
    s.seekg(2, std::ios::beg);

    std::wstring str(size / 2 + 1, L'\0');

    s.read(static_cast<char*>(static_cast<void*>(str.data())), size);

    ATL::CW2A text(str.c_str(), CP_UTF8);
    FilterBuf<'\r'> buf(text);

    std::iostream ss(&buf);

    unicodeSubtitles = true;

    return doOpenSubFile(ss, unicodeSubtitles, addIntervalCallback);
}

} // namespace

bool OpenSubtitlesFile(const TCHAR* videoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback)
{
    const auto extension = PathFindExtension(videoPathName);
    if (!_tcsicmp(extension, _T(".srt"))
        && OpenSubFile(OpenSubRipFile, videoPathName, unicodeSubtitles, addIntervalCallback))
    {
        return true;
    }

    return OpenSubFile(OpenSubStationAlphaFile, videoPathName, unicodeSubtitles, addIntervalCallback);
}

bool OpenMatchingSubtitlesFile(const TCHAR* videoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback)
{
    for (auto ext : { _T(".ass"), _T(".ssa") })
    {
        if (OpenSubFile(OpenSubStationAlphaFile, ChangePathExtension(videoPathName, ext), unicodeSubtitles, addIntervalCallback))
            return true;
    }

    const auto ext = ChangePathExtension(videoPathName, _T(".srt"));
    return OpenSubFile(OpenSubRipFile, ext, unicodeSubtitles, addIntervalCallback)
        || OpenSubFile(OpenSubStationAlphaFile, ext, unicodeSubtitles, addIntervalCallback);
}
