#pragma once

#include <memory>
#include <utility>

template<typename T, typename D>
inline auto MakeGuard(T* t, D d)
{
    return std::unique_ptr<T, D>(t, std::move(d));
}
