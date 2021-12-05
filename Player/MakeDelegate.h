#pragma once

#include <utility>

template<auto TMethod, class T>
class Delegate
{
public:
    explicit Delegate(T* callee)
        : fpCallee(callee)
    {}
    template <typename... Args>
    decltype(auto) operator()(Args&&... xs) const
    {
        return (fpCallee->*TMethod)(std::forward<Args>(xs)...);
    }

    bool operator == (const Delegate& other) const
    {
        return fpCallee == other.fpCallee;
    }

    bool operator != (const Delegate& other) const
    {
        return fpCallee != other.fpCallee;
    }

private:
    T* fpCallee;
};


template<auto TMethod, class T>
inline auto MakeDelegate(T* ptr)
{
    return Delegate<TMethod, T>(ptr);
}

#define MAKE_DELEGATE(foo, thisPtr) (MakeDelegate<foo>(thisPtr))
