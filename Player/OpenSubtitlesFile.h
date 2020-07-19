#pragma once

#include <tchar.h>
#include <functional>
#include <string>

typedef std::function<void(double, double, const std::string&)> AddIntervalCallback;


bool OpenSubtitlesFile(const TCHAR* videoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback);

bool OpenMatchingSubtitlesFile(const TCHAR* videoPathName,
    bool& unicodeSubtitles,
    AddIntervalCallback addIntervalCallback);
