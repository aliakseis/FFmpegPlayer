#include "http_get.h"

#include "httprequest_h.h"

CComVariant HttpGet(const char * url)
{
    VARIANT         varFalse;
    VARIANT         varEmpty;

    VariantInit(&varFalse);
    V_VT(&varFalse) = VT_BOOL;
    V_BOOL(&varFalse) = VARIANT_FALSE;

    VariantInit(&varEmpty);
    V_VT(&varEmpty) = VT_ERROR;

    CComVariant varBody;

    CComPtr<IWinHttpRequest> pIWinHttpRequest;

    if (SUCCEEDED(pIWinHttpRequest.CoCreateInstance(L"WinHttp.WinHttpRequest.5.1", NULL, CLSCTX_INPROC_SERVER))
        && SUCCEEDED(pIWinHttpRequest->Open(CComBSTR(L"GET"), CComBSTR(static_cast<const char*>(url)), varFalse))
        && SUCCEEDED(pIWinHttpRequest->Send(varEmpty)))
    {
        pIWinHttpRequest->get_ResponseBody(&varBody);
    }

    return varBody;
}
