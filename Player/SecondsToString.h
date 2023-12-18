#pragma once

#include <string>
#include <sstream>
#include <iomanip>

inline std::basic_string<TCHAR> secondsToString(int seconds, bool milli)
{
    using std::setfill;
    using std::setw;

    std::basic_ostringstream<TCHAR> buffer;
    if (seconds < (milli? 0 : -999))
    {
        buffer << '-';
        seconds = -seconds;
    }
    int ms = seconds % 1000;
    seconds /= 1000; // towards zero
    int s = seconds % 60;
    int m = (seconds / 60) % 60;
    int h = seconds / 3600;

    if (h > 0)
    { 
        buffer << h << ':';
    }
    buffer << setfill(_T('0')) << setw(2) << m << ':' << setfill(_T('0')) << setw(2) << s;
    if (milli)
    {
        buffer << '.' << setfill(_T('0')) << setw(3) << ms;
    }

    return buffer.str();
}
