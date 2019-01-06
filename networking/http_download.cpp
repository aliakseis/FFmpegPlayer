#include "http_download.h"

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <type_traits>
#include <system_error>
#include <vector>
#include <fstream>

//#pragma comment(lib, "Winhttp")

namespace {

auto MakeGuard(HINTERNET h)
{
    if (!h)
    {
        const DWORD errorCode = GetLastError();
        throw std::system_error(
            errorCode, std::system_category(), "HINTERNET object creation failed.");
    }

    return std::unique_ptr<std::remove_pointer_t<HINTERNET>, decltype(&WinHttpCloseHandle)>
        (h, WinHttpCloseHandle);
}

} // namespace

bool HttpDownload(const TCHAR* url, const TCHAR* path)
{
    std::wstring wszUrl(url, url + _tcslen(url));
    URL_COMPONENTS urlComp{ sizeof(urlComp) };

    // Set required component lengths to non-zero 
    // so that they are cracked.
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wszUrl.c_str(), wszUrl.length(), 0, &urlComp))
    {
        fprintf(stderr, "Error %u in WinHttpCrackUrl.\n", GetLastError());
        return false;
    }

    // Use WinHttpOpen to obtain a session handle.
    auto hSession = MakeGuard(WinHttpOpen(L"HttpDownload/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));

    // Specify an HTTP server.
    std::wstring szHostName(
        urlComp.lpszHostName, urlComp.lpszHostName + urlComp.dwHostNameLength);

    auto hConnect = MakeGuard(WinHttpConnect(hSession.get(), szHostName.c_str(),
            urlComp.nPort, 0));

    // Create an HTTP request handle.
    auto hRequest = MakeGuard(WinHttpOpenRequest(hConnect.get(), L"GET", 
            urlComp.lpszUrlPath,
            NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            (urlComp.nScheme == INTERNET_SCHEME_HTTPS)? WINHTTP_FLAG_SECURE : 0));

    // Send a request.
    BOOL bResults = WinHttpSendRequest(hRequest.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0,
            0, 0);

    // End the request.
    if (bResults)
        bResults = WinHttpReceiveResponse(hRequest.get(), NULL);

    // Keep checking for data until there is nothing left.
    if (bResults)
    {
        std::ofstream f(path, std::ofstream::binary);
        std::vector<char> buf;
        for (;;)
        {
            DWORD dwSize = 0;
            // Check for available data.
            if (!WinHttpQueryDataAvailable(hRequest.get(), &dwSize))
                fprintf(stderr, "Error %u in WinHttpQueryDataAvailable.\n",
                    GetLastError());

            if (dwSize == 0)
                break;

            // Allocate space for the buffer.
            if (buf.size() < dwSize)
                buf.resize(dwSize);

            DWORD dwDownloaded = 0;

            if (WinHttpReadData(hRequest.get(), buf.data(),
                dwSize, &dwDownloaded))
            {
                f.write(buf.data(), dwDownloaded);
            }
            else
            {
                fprintf(stderr, "Error %u in WinHttpReadData.\n", GetLastError());
            }
        }
    }


    // Report any errors.
    if (!bResults)
        fprintf(stderr, "Error %d has occurred.\n", GetLastError());

    return !!bResults;
}
