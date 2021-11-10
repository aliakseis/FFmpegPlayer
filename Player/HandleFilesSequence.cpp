#include "stdafx.h"

#include "HandleFilesSequence.h"

#include <algorithm>
#include <vector>

namespace {

auto MakeComparableConsideringNumbers(const CString& s)
{
    std::vector<unsigned int> result;

    unsigned int accum = 0;
    for (int i = 0; i < s.GetLength(); ++i)
    {
        const auto c = s[i];
        if ((c >= _T('0') && accum != 0 || c > _T('0')) && c <= _T('9'))
        {
            accum = accum * 10 + (c - _T('0'));
        }
        else
        {
            if (accum != 0)
            {
                result.push_back(accum + static_cast<unsigned int>(_T('0')));
                accum = 0;
            }
            result.push_back((static_cast<unsigned>(c) < static_cast<unsigned>(_T('9'))) 
                ? c : (0xFFFF0000 | c));
        }
    }

    if (accum != 0)
    {
        result.push_back(accum + static_cast<unsigned int>(_T('0')));
    }

    return result;
}

bool CompareConsideringNumbers(CString left, CString right)
{
    const auto leftConverted = MakeComparableConsideringNumbers(left.MakeUpper());
    const auto rightConverted = MakeComparableConsideringNumbers(right.MakeUpper());

    return std::lexicographical_compare(
        rightConverted.begin(), rightConverted.end(), leftConverted.begin(), leftConverted.end());
}

}

bool HandleFilesSequence(const CString& pathName,
    bool looping,
    std::function<bool(const CString&)> tryToOpen)
{
    const auto extension = PathFindExtension(pathName);
    const auto fileName = PathFindFileName(pathName);
    if (!extension || !fileName)
        return false;
    const CString directory(pathName, fileName - pathName);
    const CString pattern((directory + _T('*')) + extension);

    WIN32_FIND_DATA ffd{};
    const auto hFind = FindFirstFile(pattern, &ffd);

    if (INVALID_HANDLE_VALUE == hFind)
    {
        return false;
    }

    std::vector<CString> filesArr[2];
    const auto extensionLength = pathName.GetLength() - (extension - pathName);
    const CString justFileName(fileName, extension - fileName);

    do
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            const auto length = _tcslen(ffd.cFileName);
            if (length > extensionLength && ffd.cFileName[length - extensionLength] == _T('.'))
            {
                CString cFileName(ffd.cFileName, length - extensionLength);
                const bool beforeOrEqual = !CompareConsideringNumbers(cFileName, justFileName);
                filesArr[beforeOrEqual].push_back(cFileName);
            }
        }
    } while (FindNextFile(hFind, &ffd));

    FindClose(hFind);

    for (int i = 0; i <= looping; ++i)
    {
        std::vector<CString>& files = filesArr[i];
        std::make_heap(files.begin(), files.end(), CompareConsideringNumbers);

        while (!files.empty())
        {
            const CString path = directory + files.front() + extension;
            if (tryToOpen(path))
            {
                return true;
            }
            std::pop_heap(files.begin(), files.end(), CompareConsideringNumbers);
            files.pop_back();
        }
    }
    return false;
}
