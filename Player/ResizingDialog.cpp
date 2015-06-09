// ResizeDialog.cpp : implementation file
//

#include "stdafx.h"
#include "ResizingDialog.h"

namespace
{

void ScreenToClient(HWND hWnd, RECT* pRect)
{
    POINT p1 = { pRect->left, pRect->top };
    POINT p2 = { pRect->right, pRect->bottom };
    ::ScreenToClient(hWnd, &p1);
    ::ScreenToClient(hWnd, &p2);
    pRect->left = p1.x;
    pRect->top = p1.y;
    pRect->right = p2.x;
    pRect->bottom = p2.y;
}

/////////////////////////////////////////////////////////////////////////////

struct STRUCT_UpdateEnumChildProc
{
    // temporary for static function UpdateEnumChildProc:
    CRect m_client;
    long m_cx;
    long m_cy;

    ResizingDialogStrategy* m_that;
};

//////////////////////////////////////////////////////////////////////////

enum
{
    INVALID_WINDOW_SIZE = 0x10000
};

const CSize INVALID_SIZE(INVALID_WINDOW_SIZE, INVALID_WINDOW_SIZE);

}  // namespace

/////////////////////////////////////////////////////////////////////////////
// ResizingDialogStrategy dialog

ResizingDialogStrategy::ResizingDialogStrategy(HWND hWnd)
: m_hWnd(hWnd)
, m_bInitialized(false)
{
    m_FirstClientSize = { 0, 0 };
    m_OldClientSize = { 0, 0 };
}

/////////////////////////////////////////////////////////////////////////////
// ResizingDialogStrategy message handlers

BOOL CALLBACK ResizingDialogStrategy::InitializeEnumChildProc(HWND hwnd, LPARAM lParam)
{
    ResizingDialogStrategy* that = (ResizingDialogStrategy*)lParam;

    ITEM_INFO info;
    info.bInitializedManually = FALSE;
    ::GetWindowRect(hwnd, &info.rect);
    ScreenToClient(that->m_hWnd, &info.rect);
    that->m_children[hwnd] = info;

    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////

void ResizingDialogStrategy::OnInitDialog()
{
    ASSERT(m_hWnd != NULL);
    ::EnumChildWindows(m_hWnd, InitializeEnumChildProc, (LPARAM) this);

    CRect r;
    ::GetClientRect(m_hWnd, &r);
    m_FirstClientSize = r.Size();
    m_OldClientSize = r.Size();
    m_bInitialized = true;
}

//////////////////////////////////////////////////////////////////////////

void ResizingDialogStrategy::RefreshChildSizes()
{
    ASSERT(m_hWnd != NULL);
    ASSERT(m_bInitialized);
    CRect r;
    ::GetClientRect(m_hWnd, &r);
    ::SendMessage(m_hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.Width(), r.Height()));
}

//////////////////////////////////////////////////////////////////////////

bool ResizingDialogStrategy::AddChildInfo(UINT nID, COORD left, COORD top, COORD right,
                                          COORD bottom)
{
    ASSERT(m_hWnd != NULL);
    ASSERT(m_bInitialized);
    HWND hWnd = ::GetDlgItem(m_hWnd, nID);
    if (hWnd == NULL)
        return false;
    return AddChildInfo(hWnd, left, top, right, bottom);
}

//////////////////////////////////////////////////////////////////////////

bool ResizingDialogStrategy::AddChildInfo(HWND hWnd, COORD left, COORD top, COORD right,
                                          COORD bottom)
{
    ASSERT(m_bInitialized);

    auto iter = m_children.find(hWnd);
    if (iter == m_children.end())
    {
        ASSERT(false && "Can't find child window!");
        return false;
    }

    ITEM_INFO info;
    info.bInitializedManually = TRUE;
    info.rect = iter->second.rect;
    info.left = left;
    info.top = top;
    info.right = right;
    info.bottom = bottom;

    m_children[hWnd] = info;

    return true;
}

//////////////////////////////////////////////////////////////////////////

inline void ResizingDialogHelpers::CoordinateStrategy::Process(long& lResultCoordinate,
                                                               long lCurrentClientWndDelta,
                                                               long lClientWndSize,
                                                               long lFirstControlSize,
                                                               long lFirstClientWndSize) const
{
    switch (m_type)
    {
    case TYPE_CONST:
    {
        lResultCoordinate = (long)m_param;
    }
    break;

    case TYPE_FACTOR:
    {
        lResultCoordinate =
            lFirstControlSize + long(double(lClientWndSize - lFirstClientWndSize) * m_param);
    }
    break;

    case TYPE_PERCENT:
    {
        lResultCoordinate += long(double(lClientWndSize) * m_param);
    }
    break;

    default:
        ASSERT(FALSE && "Invalid state!");
        break;
    }
}

//////////////////////////////////////////////////////////////////////////

BOOL CALLBACK ResizingDialogStrategy::UpdateEnumChildProc(HWND hwnd, LPARAM lParam)
{
    STRUCT_UpdateEnumChildProc* pS = (STRUCT_UpdateEnumChildProc*)lParam;
    ResizingDialogStrategy* that = pS->m_that;

    auto iter = that->m_children.find(hwnd);
    if (iter == that->m_children.end())
    {
        return TRUE;
    }

    const ITEM_INFO& info = iter->second;
    if (!info.bInitializedManually)
    {
        using namespace ResizingDialogHelpers;
        ASSERT(info.left == Coord());
        ASSERT(info.top == Coord());
        ASSERT(info.right == Coord());
        ASSERT(info.bottom == Coord());
        return TRUE;
    }

    CRect r;
    ::GetWindowRect(hwnd, &r);
    ScreenToClient(that->m_hWnd, &r);

    long width = pS->m_client.Width();
    long height = pS->m_client.Height();
    SIZE FirstClientSize = that->m_FirstClientSize;

    info.left.Process(r.left, pS->m_cx, width, info.rect.left, FirstClientSize.cx);
    info.top.Process(r.top, pS->m_cy, height, info.rect.top, FirstClientSize.cy);
    info.right.Process(r.right, pS->m_cx, width, info.rect.right, FirstClientSize.cx);
    info.bottom.Process(r.bottom, pS->m_cy, height, info.rect.bottom, FirstClientSize.cy);

    ::MoveWindow(hwnd, r.left, r.top, r.Width(), r.Height(), TRUE);

    ::InvalidateRect(hwnd, NULL, TRUE);

    return TRUE;
}

//////////////////////////////////////////////////////////////////////////

void ResizingDialogStrategy::OnSize(UINT nType, int cx, int cy)
{
    if (!m_bInitialized)
    {
        return;
    }

    STRUCT_UpdateEnumChildProc s;

    ASSERT(m_hWnd != NULL);
    GetClientRect(m_hWnd, &s.m_client);
    int w = s.m_client.Width();
    int h = s.m_client.Height();
    s.m_cx = w - m_OldClientSize.cx;
    s.m_cy = h - m_OldClientSize.cy;
    m_OldClientSize = s.m_client.Size();
    s.m_that = this;

    ::EnumChildWindows(m_hWnd, UpdateEnumChildProc, (LPARAM)&s);
}

//////////////////////////////////////////////////////////////////////////

MinMaxDialogStrategy::MinMaxDialogStrategy()
{
    m_minSize = INVALID_SIZE;
    m_maxSize = INVALID_SIZE;
};

//////////////////////////////////////////////////////////////////////////

void MinMaxDialogStrategy::SetWindowMinSize(SIZE sz) { m_minSize = sz; }

//////////////////////////////////////////////////////////////////////////

void MinMaxDialogStrategy::SetWindowMaxSize(SIZE sz) { m_maxSize = sz; }

//////////////////////////////////////////////////////////////////////////

void MinMaxDialogStrategy::OnGetMinMaxInfo(MINMAXINFO* pMinMax)
{
    if (INVALID_SIZE != m_minSize)
        pMinMax->ptMinTrackSize = CPoint(m_minSize);

    if (INVALID_SIZE != m_maxSize)
        pMinMax->ptMaxTrackSize = CPoint(m_maxSize);
}
