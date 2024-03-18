#pragma once

// vcpkg install dtl
#include <dtl/dtl.hpp>

#include <filesystem>


class StringDifference
{
    typedef std::basic_string<TCHAR> Sequence;

    dtl::Diff<TCHAR, std::basic_string<TCHAR>> m_diff, m_reversedDiff;
    std::filesystem::path m_path_b;
    bool m_sameNames;

    Sequence LastResort(const StringDifference::Sequence& seq) const;

   public:
    StringDifference(const Sequence& a, const Sequence& b);
    Sequence patch(const Sequence& seq) const;
};
