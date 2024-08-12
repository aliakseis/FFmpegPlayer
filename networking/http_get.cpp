#include "http_get.h"

#include "httprequest_h.h"

#include <ws2tcpip.h>

#include <algorithm>

namespace {

const wchar_t USER_AGENT[]
    = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/77.0.3865.75 Safari/537.36";

class CComUsageScope
{
    bool m_bInitialized;
public:
    explicit CComUsageScope(DWORD dwCoInit = COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY)
    {
        m_bInitialized = SUCCEEDED(CoInitializeEx(NULL, dwCoInit));
    }
    ~CComUsageScope()
    {
        if (m_bInitialized)
            CoUninitialize();
    }
};

std::string resolveHostnameToIP(const std::string& hostname) {
    addrinfo hints = { 0 }, * res = nullptr;
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) != 0) {
        return {};
    }

    char ipStr[INET6_ADDRSTRLEN];
    void* addr;

    if (res->ai_family == AF_INET) { // IPv4
        sockaddr_in* sockaddr_ipv4 = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        addr = &(sockaddr_ipv4->sin_addr);
    }
    else if (res->ai_family == AF_INET6) { // IPv6
        sockaddr_in6* sockaddr_ipv6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
        addr = &(sockaddr_ipv6->sin6_addr);
    }
    else {
        freeaddrinfo(res);
        return {};
    }

    inet_ntop(res->ai_family, addr, ipStr, sizeof(ipStr));
    freeaddrinfo(res);

    return std::string(ipStr);
}

} // namespace


long HttpGetStatus(std::string& url, bool useSAN)
{
    if (useSAN)
    {
        auto matchEnd = std::mismatch(url.begin(), url.end(), "https://", 
            [](char c1, char c2) { return tolower(c1) == tolower(c2); });
        if (matchEnd.first == url.end() || *matchEnd.second != '\0')
        {
            useSAN = false;
        }
    }

    CComBSTR bstrUrl;
    CComBSTR bstrHostname;

    if (useSAN)
    {
        const auto pos = 8; // Move past "://"
        size_t endPos = url.find_first_of(":/", pos);
        if (endPos != std::string::npos)
        {
            std::string hostname = url.substr(pos, endPos - pos);
            std::string ip = resolveHostnameToIP(hostname);
            if (!ip.empty()) {
                bstrUrl = (url.substr(0, pos) + ip + url.substr(endPos)).c_str();
                bstrHostname = hostname.c_str();
            }
            else
            {
                useSAN = false;
            }
        }
        else
        {
            useSAN = false;
        }
    }

    if (!useSAN)
    {
        bstrUrl = url.c_str();
    }


    VARIANT varFalse{ VT_BOOL };
    VARIANT varEmpty{ VT_ERROR };
    VARIANT varOption{ VT_I4 };
    varOption.intVal = 0x1000;

    HRESULT result = 0;

    CComPtr<IWinHttpRequest> pIWinHttpRequest;

    CComUsageScope scope;

    if (FAILED(result = pIWinHttpRequest.CoCreateInstance(L"WinHttp.WinHttpRequest.5.1", NULL, CLSCTX_INPROC_SERVER)))
        return result;

    if (FAILED(result = pIWinHttpRequest->Open(CComBSTR(L"HEAD"), bstrUrl, varFalse)))
        return result;

    if (useSAN && FAILED(result = pIWinHttpRequest->SetRequestHeader(CComBSTR(L"Host"), bstrHostname)))
        return result;

    if (FAILED(result = pIWinHttpRequest->SetRequestHeader(CComBSTR(L"User-Agent"), CComBSTR(USER_AGENT))))
        return result;

    if (useSAN && FAILED(result = pIWinHttpRequest->put_Option(WinHttpRequestOption_SslErrorIgnoreFlags, varOption))) // Ignore SSL errors
        return result;

    if (FAILED(result = pIWinHttpRequest->put_Option(WinHttpRequestOption_EnableRedirects, varFalse)))
        return result;

    if (SUCCEEDED(result = pIWinHttpRequest->Send(varEmpty)))
    {
        pIWinHttpRequest->get_Status(&result);
        if (result == 302)
        {
            CComBSTR locationHeader;
            if (SUCCEEDED(pIWinHttpRequest->GetResponseHeader(CComBSTR(L"Location"), &locationHeader)))
            {
                url = CW2A(locationHeader);
            }
        }
    }

    return result;
}

CComVariant HttpGet(const char * url)
{
    VARIANT varFalse{ VT_BOOL };
    VARIANT varEmpty{ VT_ERROR };

    CComVariant varBody;

    CComPtr<IWinHttpRequest> pIWinHttpRequest;

    CComUsageScope scope;

    if (SUCCEEDED(pIWinHttpRequest.CoCreateInstance(L"WinHttp.WinHttpRequest.5.1", NULL, CLSCTX_INPROC_SERVER))
        && SUCCEEDED(pIWinHttpRequest->Open(CComBSTR(L"GET"), CComBSTR(static_cast<const char*>(url)), varFalse))
        && SUCCEEDED(pIWinHttpRequest->SetRequestHeader(CComBSTR(L"User-Agent"), CComBSTR(USER_AGENT)))
        && SUCCEEDED(pIWinHttpRequest->Send(varEmpty)))
    {
        pIWinHttpRequest->get_ResponseBody(&varBody);
    }

    return varBody;
}
