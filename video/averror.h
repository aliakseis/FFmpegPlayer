#pragma once

#include <ostream>

extern "C" {
#include <libavutil/error.h>
}

struct AvError
{
    int code;
};

inline std::ostream& operator<<(std::ostream& os, const AvError& err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];

    if (av_strerror(err.code, buf, sizeof(buf)) == 0)
        os << ": " << buf;

    return os;
}
