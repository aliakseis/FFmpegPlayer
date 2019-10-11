#pragma once

#include <tchar.h>
#include <functional>
#include <string>

typedef std::function<void(double, double, const std::string&)> AddIntervalCallback;

bool OpenSubRipFile(const TCHAR* lpszVideoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback);

bool OpenSubStationAlphaFile(const TCHAR* lpszVideoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback);
