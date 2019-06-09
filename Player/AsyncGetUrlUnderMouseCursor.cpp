#include "stdafx.h"

#include "AsyncGetUrlUnderMouseCursor.h"

namespace {

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


VOID CALLBACK SendAsyncProc(
    HWND,
    UINT,
    ULONG_PTR dwData,
    LRESULT lResult)
{
    CComUsageScope scope;

    CComPtr<IAccessible> pacc;
    if (FAILED(ObjectFromLresult(lResult, __uuidof(IAccessible), 0, (void**)&pacc)))
        return;

    POINT ptScreen{ LOWORD(dwData), HIWORD(dwData) };
    {
        CComVariant vtChild;
        CComQIPtr<IAccessible> paccChild;
        for (; SUCCEEDED(pacc->accHitTest(ptScreen.x, ptScreen.y, &vtChild))
            && VT_DISPATCH == vtChild.vt && (paccChild = vtChild.pdispVal) != NULL;
            vtChild.Clear())
        {
            pacc.Attach(paccChild.Detach());
        }
    }

    VARIANT v;
    v.vt = VT_I4;
    v.lVal = CHILDID_SELF;

    while (pacc)
    {
        CComVariant vRole;

        if (SUCCEEDED(pacc->get_accRole(v, &vRole)) && vRole.vt == VT_I4 && vRole.lVal == ROLE_SYSTEM_LINK)
        {
            CComBSTR url;
            if (FAILED(pacc->get_accValue(v, &url)))
                return;

            AfxGetApp()->PostThreadMessage(WM_ON_ASYNC_URL, (WPARAM)url.Detach(), NULL);
            return;
        }

        CComPtr<IDispatch> spDisp;
        if (FAILED(pacc->get_accParent(&spDisp)))
            return;
        CComQIPtr<IAccessible> spParent(spDisp);
        pacc.Attach(spParent.Detach());
    }
}

} // namespace


void AsyncGetUrlUnderMouseCursor()
{
    POINT pt;
    if (!GetCursorPos(&pt))
        return;

    HWND hWnd = WindowFromPoint(pt);
    if (NULL == hWnd)
        return;

    TCHAR szBuffer[64];

    const int classNameLength
        = ::GetClassName(hWnd, szBuffer, sizeof(szBuffer) / sizeof(szBuffer[0]));

    szBuffer[sizeof(szBuffer) / sizeof(szBuffer[0]) - 1] = _T('\0');

    if (_tcscmp(szBuffer, _T("MozillaWindowClass")) != 0 && _tcscmp(szBuffer, _T("Chrome_RenderWidgetHostHWND")) != 0)
        return;

    VERIFY(SendMessageCallback(hWnd, WM_GETOBJECT, 0L, OBJID_CLIENT, SendAsyncProc, MAKELONG(pt.x, pt.y)));
}
