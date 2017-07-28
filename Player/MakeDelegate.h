#pragma once

// http://blog.coldflake.com/posts/C++-delegates-on-steroids/

#include <utility>

template<typename return_type, typename... params>
struct DelegateScope
{
    template <class T, return_type(T::*TMethod)(params...)>
    class Delegate
    {
    public:
        explicit Delegate(T* callee)
            : fpCallee(callee)
        {}
        //return_type operator()(params... xs) const
        //{
        //    return (fpCallee->*TMethod)(xs...);
        //}
        template <typename... Args>
        return_type operator()(Args&&... xs) const
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
};

template<typename T, typename return_type, typename... params>
struct DelegateMaker
{
    template<return_type(T::*foo)(params...)>
    inline static auto Bind(T* o)
    {
        return DelegateScope<return_type, params...>::Delegate<T, foo>(o);
    }
};

template<typename T, typename return_type, typename... params>
DelegateMaker<T, return_type, params... >
makeDelegate(return_type(T::*)(params...))
{
    return DelegateMaker<T, return_type, params...>();
}

#define MAKE_DELEGATE(foo, thisPrt) (makeDelegate(foo).Bind<foo>(thisPrt))
