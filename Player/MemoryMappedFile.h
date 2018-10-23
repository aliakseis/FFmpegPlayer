#pragma once

class MemoryMappedFile
{
public:
    MemoryMappedFile()
        : hFile(INVALID_HANDLE_VALUE)
        , hFileMapping(NULL)
        , pData(NULL)
    {}
    ~MemoryMappedFile()
    {
        if (pData)
            UnmapViewOfFile(pData);
        if (hFileMapping)
            CloseHandle(hFileMapping);
        if (INVALID_HANDLE_VALUE != hFile)
            CloseHandle(hFile);
    }
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    bool MapFlie(LPCTSTR szFile)
    {
        hFile = CreateFile(
            szFile,                 // pointer to name of the file
            GENERIC_READ,           // access (read-write) mode
            FILE_SHARE_READ,        // share mode
            NULL,                   // pointer to security attributes
            OPEN_EXISTING,          // how to create
            FILE_ATTRIBUTE_NORMAL,  // file attributes
            NULL                    // handle to file with attributes to copy
        );
        if (INVALID_HANDLE_VALUE != hFile && GetFileSizeEx(hFile, &fileSize) && fileSize.HighPart == 0)
        {
            hFileMapping = CreateFileMapping(
                hFile,              // handle to file to map
                NULL,               // optional security attributes
                PAGE_READONLY,      // protection for mapping object
                fileSize.HighPart,  // high-order 32 bits of object size
                fileSize.LowPart,   // low-order 32 bits of object size
                NULL                // name of file-mapping object
            );
            if (NULL != hFileMapping)
            {
                pData = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, (SIZE_T)fileSize.QuadPart);
                return pData != NULL;
            }
        }

        return false;
    }

    LPVOID data() const { return pData; }
    LONGLONG size() const { return fileSize.QuadPart; }

private:
    HANDLE hFile;
    LARGE_INTEGER fileSize;
    HANDLE hFileMapping;
    LPVOID pData;
};
