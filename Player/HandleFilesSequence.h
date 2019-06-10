#pragma once

#include <functional>

bool HandleFilesSequence(const CString& pathName,
    bool looping,
    std::function<bool(const CString&)> tryToOpen);
