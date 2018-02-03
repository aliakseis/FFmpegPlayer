#pragma once

#include <boost/atomic.hpp>

inline void InterlockedAdd(boost::atomic<double>& clock, double correction)
{
    for (double v = clock; !clock.compare_exchange_weak(v, v + correction);)
    {
    }
}

