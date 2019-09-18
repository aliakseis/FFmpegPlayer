#include "http_get.h"

#include "httprequest_h.h"

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

} // namespace


long HttpGetStatus(const char * url)
{
    VARIANT varFalse{ VT_BOOL };
    VARIANT varEmpty{ VT_ERROR };

    HRESULT result = 0;

    CComPtr<IWinHttpRequest> pIWinHttpRequest;

    CComUsageScope scope;

    if (SUCCEEDED(result = pIWinHttpRequest.CoCreateInstance(L"WinHttp.WinHttpRequest.5.1", NULL, CLSCTX_INPROC_SERVER))
        && SUCCEEDED(result = pIWinHttpRequest->Open(CComBSTR(L"HEAD"), CComBSTR(static_cast<const char*>(url)), varFalse))
        && SUCCEEDED(result = pIWinHttpRequest->SetRequestHeader(CComBSTR(L"User-Agent"), CComBSTR(USER_AGENT)))
        && SUCCEEDED(result = pIWinHttpRequest->Send(varEmpty)))
    {
        pIWinHttpRequest->get_Status(&result);
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
