#include "stdafx.h"
#include "StringDifference.h"

#include <algorithm>

static auto SafePathString(std::basic_string<TCHAR> path)
{
    enum
    {
        MAX_DIFF_SIZE = 2048
    };
    path.append(MAX_DIFF_SIZE, _T('\0'));
    return path;
}


template <typename T>
T reversed(const T& s)
{
    return {s.rbegin(), s.rend()};
}

template<typename T>
int levenshteinDistance(const T& s1, const T& s2)
{
    const int len1 = s1.size() + 1;
    const int len2 = s2.size() + 1;
    std::vector<int> dp(len1 * len2);

    for (int i = 0; i < len1; ++i)
    {
        dp[i * len2] = i;
    }
    for (int j = 0; j < len2; ++j)
    {
        dp[j] = j;
    }

    for (int i = 1; i < len1; ++i)
    {
        for (int j = 1; j < len2; ++j)
        {
            const int cost = (s1[i - 1] != s2[j - 1]);
            dp[i * len2 + j] = (std::min)({dp[(i - 1) * len2 + j] + 1,
                dp[i * len2 + j - 1] + 1, dp[(i - 1) * len2 + j - 1] + cost});
        }
    }

    return dp.back();
}

StringDifference::StringDifference(const Sequence& a, const Sequence& b)
    : m_diff(a, b),
      m_reversedDiff(reversed(a), reversed(b)),
      m_path_b(b),
      m_sameNames(m_path_b.has_parent_path() && m_path_b.has_stem() &&
                  m_path_b.stem() == std::filesystem::path(a).stem())
{
    if (!m_sameNames)
    {
        m_diff.compose();
        m_reversedDiff.compose();
    }
}

StringDifference::Sequence StringDifference::patch(const Sequence& seq) const
{
    if (m_sameNames)
    {
        return (m_path_b.parent_path() / std::filesystem::path(seq).stem()) +=
               m_path_b.extension();
    }

    Sequence s = m_reversedDiff.patch(SafePathString(reversed(seq)));
    std::reverse(s.begin(), s.begin() + _tcslen(s.c_str()));
    if (s.empty() || s[0] == 0 || 0 != _taccess(s.c_str(), 04))
    {
        s = m_diff.patch(SafePathString(seq));
        if (s.empty() || s[0] == 0 || 0 != _taccess(s.c_str(), 04))
        {
            return LastResort(seq);
        }
    }
    s.resize(_tcslen(s.c_str()));
    return s;
}

StringDifference::Sequence StringDifference::LastResort(
    const StringDifference::Sequence& seq) const
{
    const auto extension = m_path_b.extension();
    const Sequence name = std::filesystem::path(seq).stem();
    int minDistance = INT_MAX;
    std::filesystem::path result_name;
    for (auto const& dir_entry : std::filesystem::directory_iterator{m_path_b.parent_path()})
    {
        if (dir_entry.is_directory())
            continue;
        auto path = dir_entry.path();
        if (_tcsicmp(path.extension().c_str(), extension.c_str()) != 0)
            continue;
        const auto stem = path.stem();
        const int dist = levenshteinDistance(name, static_cast<const Sequence&>(stem));
        if (dist < minDistance)
        {
            minDistance = dist;
            result_name = stem;
        }
        else if (dist == minDistance)
        {
            result_name.clear();
        }
    }
    if (!result_name.empty())
    {
        auto result = (m_path_b.parent_path() / result_name) += extension;
        if (equivalent(m_path_b, result))
            return {};
        return result;
    }
    return {};
}
