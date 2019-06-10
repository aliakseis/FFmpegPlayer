#include "stdafx.h"

#include "HandleFilesSequence.h"

#include <algorithm>
#include <vector>

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
                ffd.cFileName[length - extensionLength] = 0;
                const bool beforeOrEqual = _tcsicmp(ffd.cFileName, justFileName) <= 0;
                filesArr[beforeOrEqual].emplace_back(ffd.cFileName, length - extensionLength);
            }
        }
    } while (FindNextFile(hFind, &ffd));

    FindClose(hFind);

    auto comp = [](const CString& left, const CString& right) {
        return left.CompareNoCase(right) > 0;
    };

    for (int i = 0; i <= looping; ++i)
    {
        std::vector<CString>& files = filesArr[i];
        std::make_heap(files.begin(), files.end(), comp);

        while (!files.empty())
        {
            const CString path = directory + files.front() + extension;
            if (tryToOpen(path))
            {
                return true;
            }
            std::pop_heap(files.begin(), files.end(), comp);
            files.pop_back();
        }
    }
    return false;
}
