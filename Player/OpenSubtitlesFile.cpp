#include "stdafx.h"

#include "OpenSubtitlesFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

bool OpenSubRipFile(const TCHAR* lpszVideoPathName,
    AddIntervalCallback addIntervalCallback,
    bool& unicodeSubtitles)
{
    CString subRipPathName(lpszVideoPathName);
    PathRemoveExtension(subRipPathName.GetBuffer());
    subRipPathName.ReleaseBuffer();
    subRipPathName += _T(".srt");

    std::ifstream s(subRipPathName);
    if (!s)
        return false;

    std::string buffer;
    bool first = true;
    while (std::getline(s, buffer))
    {
        if (first)
        {
            unicodeSubtitles = buffer.length() > 2
                && buffer[0] == char(0xEF) && buffer[1] == char(0xBB) && buffer[2] == char(0xBF);
            first = false;
        }

        if (std::find_if_not(buffer.begin(), buffer.end(), static_cast<int(*)(int)>(std::isspace))
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

        if (!subtitle.empty())
        {
            addIntervalCallback(start, end, subtitle);
        }
    }

    return true;
}

bool OpenSubStationAlphaFile(const TCHAR* lpszVideoPathName,
    AddIntervalCallback addIntervalCallback,
    bool& unicodeSubtitles)
{
    for (auto ext : { _T(".ass"), _T(".ssa") })
    {
        CString subRipPathName(lpszVideoPathName);
        PathRemoveExtension(subRipPathName.GetBuffer());
        subRipPathName.ReleaseBuffer();
        subRipPathName += ext;

        std::ifstream s(subRipPathName);
        if (!s)
            continue;

        std::string buffer;
        bool first = true;
        while (std::getline(s, buffer))
        {
            if (first)
            {
                unicodeSubtitles = buffer.length() > 2
                    && buffer[0] == char(0xEF) && buffer[1] == char(0xBB) && buffer[2] == char(0xBF);
                first = false;
            }

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

            std::string subtitle;
            if (!std::getline(ss, subtitle, '\\'))
                continue;
            while (std::getline(ss, buffer, '\\'))
            {
                subtitle +=
                    (buffer[0] == 'N' || buffer[0] == 'n') ? '\n' + buffer.substr(1) : '\\' + buffer;
            }

            if (!subtitle.empty())
            {
                subtitle += '\n'; // The last '\n' is for aggregating overlapped subtitles (if any)
                addIntervalCallback(start, end, subtitle);
            }
        }

        return true;
    }

    return false;
}
