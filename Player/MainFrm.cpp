
// MainFrm.cpp : implementation of the CMainFrame class
//

#include "stdafx.h"
#include "Player.h"

#include "MainFrm.h"
#include "PlayerDoc.h"
#include "IEraseableArea.h"
#include "MakeDelegate.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

HICON LoadIcon(int idr)
{
    int const cxButton = GetSystemMetrics(SM_CXSMICON);
    return (HICON) LoadImage(
        AfxGetApp()->m_hInstance,
        MAKEINTRESOURCE(idr),
        IMAGE_ICON,
        cxButton, cxButton, // use actual size
        LR_DEFAULTCOLOR);
}


class FullScreenBarAccessor : public CFullScreenImpl
{
public:
    CMFCToolBar* GetFullScreenBar() { return m_pwndFullScreenBar; }
};

class FullScreenMgrAccessor : public CFrameImpl
{
public:
    CMFCToolBar* GetFullScreenBar() 
    { 
        return static_cast<FullScreenBarAccessor&>(m_FullScreenMgr).GetFullScreenBar(); 
    }
};

class DocumentAccessor : public CView
{
public:
    CPlayerDoc* GetDocument()
    {
        return dynamic_cast<CPlayerDoc*>(m_pDocument);
    }
};

UINT s_uTBBC = RegisterWindowMessage(L"TaskbarButtonCreated");

} // namespace

// CMainFrame

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWndEx)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWndEx)
    ON_WM_CREATE()
    ON_WM_CLOSE()
    ON_COMMAND(IDC_FULL_SCREEN, &CMainFrame::OnFullScreen)
    ON_WM_ERASEBKGND()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_NCPAINT()
    ON_WM_POWERBROADCAST()
    ON_REGISTERED_MESSAGE(s_uTBBC, &CMainFrame::CreateThumbnailToolbar)
    ON_WM_NCHITTEST()
    ON_WM_INITMENUPOPUP()
END_MESSAGE_MAP()

static UINT indicators[] =
{
    ID_SEPARATOR,           // status line indicator
    ID_INDICATOR_CAPS,
    ID_INDICATOR_NUM,
    ID_INDICATOR_SCRL,
};

// CMainFrame construction/destruction

CMainFrame::CMainFrame()
    : m_bFullScreen(FALSE)
{
    // TODO: add member initialization code here
}

CMainFrame::~CMainFrame()
{
}

BOOL CMainFrame::Create(LPCTSTR lpszClassName,
    LPCTSTR lpszWindowName,
    DWORD dwStyle,
    const RECT& rect,
    CWnd* pParentWnd,        // != NULL for popups
    LPCTSTR lpszMenuName,
    DWORD dwExStyle,
    CCreateContext* pContext)
{
    const BOOL result = __super::Create(lpszClassName,
        lpszWindowName,
        dwStyle,
        rect,
        pParentWnd,        // != NULL for popups
        lpszMenuName,
        dwExStyle,
        pContext);

    if (result)
    {
        ASSERT(pContext->m_pCurrentDoc);
        ASSERT(pContext->m_pCurrentDoc->IsKindOf(RUNTIME_CLASS(CPlayerDoc)));
        m_wndPlayerControl.setDocument(static_cast<CPlayerDoc*>(pContext->m_pCurrentDoc));
        m_wndRange.setDocument(static_cast<CPlayerDoc*>(pContext->m_pCurrentDoc));
        static_cast<CPlayerDoc*>(pContext->m_pCurrentDoc)->onPauseResume.connect(
            MAKE_DELEGATE(&CMainFrame::onPauseResume, this));
    }

    return result;
}

BOOL CMainFrame::OnCommand(WPARAM wParam, LPARAM lParam)
{
    if (LOWORD(wParam) == IDC_PLAY_PAUSE)
    {
        if (CView* pView = dynamic_cast<CView*>(GetDescendantWindow(AFX_IDW_PANE_FIRST, TRUE)))
        {
            if (CPlayerDoc* pDoc = static_cast<DocumentAccessor*>(pView)->GetDocument())
            {
                if (pDoc->isPlaying())
                    pDoc->pauseResume();
            }
        }
        return TRUE;
    }
    return __super::OnCommand(wParam, lParam);
}


int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (__super::OnCreate(lpCreateStruct) == -1)
        return -1;

    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows7));
    //CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerWindows7::Office2007_ObsidianBlack);

    if (!m_wndMenuBar.Create(this))
    {
        TRACE0("Failed to create menubar\n");
        return -1;      // fail to create
    }
    m_wndMenuBar.SetPaneStyle(m_wndMenuBar.GetPaneStyle() | CBRS_SIZE_DYNAMIC | CBRS_TOOLTIPS | CBRS_FLYBY);

    //if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC) ||
    //	!m_wndToolBar.LoadToolBar(IDR_MAINFRAME))
    //{
    //	TRACE0("Failed to create toolbar\n");
    //	return -1;      // fail to create
    //}

    //if (!m_wndStatusBar.Create(this))
    //{
    //    TRACE0("Failed to create status bar\n");
    //    return -1;      // fail to create
    //}
    //m_wndStatusBar.SetIndicators(indicators, sizeof(indicators)/sizeof(UINT));

    if (!m_wndPlayerControl.Create(
        this, 
        IDD_DIALOGBAR_PLAYER_CONTROL, 
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_LEFT | CBRS_FLOAT_MULTI,
        IDD_DIALOGBAR_PLAYER_CONTROL))
    {
        TRACE0("Failed to create player control dialog bar\n");
        return -1;      // fail to create
    }

    if (!m_wndRange.Create(
        this,
        IDD_DIALOGBAR_RANGE,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_LEFT | CBRS_FLOAT_MULTI,
        IDD_DIALOGBAR_RANGE))
    {
        TRACE0("Failed to create range dialog bar\n");
        return -1;      // fail to create
    }

    // TODO: Delete these three lines if you don't want the toolbar to be dockable
    m_wndMenuBar.EnableDocking(CBRS_ALIGN_ANY);
    //m_wndToolBar.EnableDocking(CBRS_ALIGN_TOP);
    m_wndPlayerControl.EnableDocking(CBRS_ALIGN_BOTTOM);
    m_wndRange.EnableDocking(CBRS_ALIGN_BOTTOM);

    CString strBuffer;
    VERIFY(strBuffer.LoadString(IDS_PLAYER_CONTROL));
    m_wndPlayerControl.SetWindowText(strBuffer);

    VERIFY(strBuffer.LoadString(IDS_RANGE));
    m_wndRange.SetWindowText(strBuffer);

    EnableDocking(CBRS_ALIGN_TOP | CBRS_ALIGN_BOTTOM);
    DockPane(&m_wndMenuBar);
    //DockPane(&m_wndToolBar, AFX_IDW_DOCKBAR_TOP);
    DockPane(&m_wndPlayerControl, AFX_IDW_DOCKBAR_BOTTOM);
    DockPane(&m_wndRange, AFX_IDW_DOCKBAR_BOTTOM);

    // Enable toolbar and docking window menu replacement
    VERIFY(strBuffer.LoadString(IDS_TOOLBAR_CUSTOMIZE));
    //m_wndToolBar.EnableCustomizeButton(TRUE, ID_VIEW_CUSTOMIZE, strCustomize);
    EnablePaneMenu(TRUE, ID_VIEW_CUSTOMIZE, strBuffer, ID_VIEW_TOOLBAR);

    m_hPlay = LoadIcon(IDI_PLAY);
    m_hPause = LoadIcon(IDI_PAUSE);

    // In case the application is run elevated, allow the
    // TaskbarButtonCreated and WM_COMMAND messages through.
    ChangeWindowMessageFilter(s_uTBBC, MSGFLT_ADD);
    ChangeWindowMessageFilter(WM_COMMAND, MSGFLT_ADD);

    EnableFullScreenMode(IDC_FULL_SCREEN);
    EnableFullScreenMainMenu(FALSE);

    return 0;
}

void CMainFrame::OnClose()
{
    m_pTaskbarList.Release();
    __super::OnClose();
}

LRESULT CMainFrame::CreateThumbnailToolbar(WPARAM, LPARAM)
{
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pTaskbarList));
    if (SUCCEEDED(hr))
    {
        hr = m_pTaskbarList->HrInit();
        if (SUCCEEDED(hr))
        {
            enum { NUM_ICONS = 1 };
            int const cxButton = GetSystemMetrics(SM_CXSMICON);
            if (auto himl = ImageList_Create(cxButton, cxButton, ILC_MASK, NUM_ICONS, 0))
            {
                hr = m_pTaskbarList->ThumbBarSetImageList(*this, himl);
                if (SUCCEEDED(hr))
                {
                    THUMBBUTTON buttons[NUM_ICONS] = {};

                    // First button
                    buttons[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
                    buttons[0].dwFlags = THBF_ENABLED | THBF_DISMISSONCLICK;
                    buttons[0].iId = IDC_PLAY_PAUSE;
                    buttons[0].hIcon = m_hPause;
                    CStringW strBuffer;
                    VERIFY(strBuffer.LoadString(IDS_PAUSE));
                    wcscpy_s(buttons[0].szTip, strBuffer);

                    // Set the buttons to be the thumbnail toolbar
                    hr = m_pTaskbarList->ThumbBarAddButtons(*this, ARRAYSIZE(buttons), buttons);
                }
                ImageList_Destroy(himl);
            }
        }
    }

    return TRUE;
}

void CMainFrame::onPauseResume(bool paused)
{
    if (m_pTaskbarList)
    {
        THUMBBUTTON buttons[1] = {};

        // First button
        buttons[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        buttons[0].dwFlags = THBF_ENABLED | THBF_DISMISSONCLICK;
        buttons[0].iId = IDC_PLAY_PAUSE;
        buttons[0].hIcon = paused? m_hPlay : m_hPause;
        CStringW strBuffer;
        VERIFY(strBuffer.LoadString(paused? IDS_PLAY : IDS_PAUSE));
        wcscpy_s(buttons[0].szTip, strBuffer);
        m_pTaskbarList->ThumbBarUpdateButtons(*this, 1, buttons);
    }
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if( !__super::PreCreateWindow(cs) )
        return FALSE;
    // TODO: Modify the Window class or styles here by modifying
    //  the CREATESTRUCT cs

    return TRUE;
}

// CMainFrame diagnostics

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
    __super::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
    __super::Dump(dc);
}
#endif //_DEBUG


// CMainFrame message handlers

void CMainFrame::OnFullScreen()
{
    ModifyStyle(WS_OVERLAPPEDWINDOW, 0, SWP_FRAMECHANGED);
    const bool semiTransparentMode
        = GetAsyncKeyState(VK_SHIFT) < 0 && GetAsyncKeyState(VK_CONTROL) < 0;
    if (semiTransparentMode)
    {
        SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ModifyStyleEx(0, WS_EX_LAYERED | WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(0, (255 * 40) / 100, LWA_ALPHA);
    }
    ShowFullScreen();
    if (CMFCToolBar* toolBar = static_cast<FullScreenMgrAccessor&>(m_Impl).GetFullScreenBar())
    {
        if (auto pFrame = toolBar->GetParentMiniFrame())
        {
            if (semiTransparentMode)
            {
                m_dockManager.RemoveMiniFrame(pFrame);
            }
            else
            {
                pFrame->ShowWindow(SW_HIDE);
            }
        }
    }
}


BOOL CMainFrame::OnEraseBkgnd(CDC* pDC)
{
    CWnd* pWnd = GetDescendantWindow(AFX_IDW_PANE_FIRST, TRUE);
    if (IEraseableArea* pEraseableArea = dynamic_cast<IEraseableArea*>(pWnd))
    {
        pEraseableArea->OnErase(this, pDC, IsFullScreen());
    }

    return TRUE;
}


void CMainFrame::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CFrameWndEx::OnWindowPosChanged(lpwndpos);

    // message handler code here
    if (!IsFullScreen() && m_bFullScreen)
    {
        ModifyStyle(0, WS_OVERLAPPEDWINDOW, 0);
        // clear semi transparent settings
        SetWindowPos(&wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ModifyStyleEx(WS_EX_LAYERED | WS_EX_TRANSPARENT, 0);
    }
    m_bFullScreen = IsFullScreen();
}


void CMainFrame::OnNcPaint()
{
    if (!IsFullScreen())
        Default();
}


UINT CMainFrame::OnPowerBroadcast(UINT nPowerEvent, LPARAM nEventData)
{
    if (nPowerEvent == PBT_APMSUSPEND)
    {
        if (CView* pView = dynamic_cast<CView*>(GetDescendantWindow(AFX_IDW_PANE_FIRST, TRUE)))
        {
            if (CPlayerDoc* pDoc = static_cast<DocumentAccessor*>(pView)->GetDocument())
            {
                if (pDoc->isPlaying() && !pDoc->isPaused())
                    pDoc->pauseResume();
                ShowWindow(SW_MINIMIZE);
            }
        }
    }
    return CFrameWndEx::OnPowerBroadcast(nPowerEvent, nEventData);
}

LRESULT CMainFrame::OnNcHitTest(CPoint point)
{
    enum { SPOT_SIZE = 8 };
    CRect rect;
    GetClientRect(rect);
    rect.left = rect.right - SPOT_SIZE;
    rect.top = rect.bottom - SPOT_SIZE;
    rect.right += SPOT_SIZE;
    rect.bottom += SPOT_SIZE;
    ClientToScreen(&rect);

    if (rect.PtInRect(point))
    {
        BOOL bRTL = GetExStyle() & WS_EX_LAYOUTRTL;
        return bRTL ? HTBOTTOMLEFT : HTBOTTOMRIGHT;
    }

    return __super::OnNcHitTest(point);
}


void CMainFrame::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
    CFrameWndEx::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);

    if (bSysMenu || pPopupMenu->GetMenuItemCount() != 1)
        return;

    if (CView* pView = dynamic_cast<CView*>(GetDescendantWindow(AFX_IDW_PANE_FIRST, TRUE)))
    {
        if (CPlayerDoc* pDoc = static_cast<DocumentAccessor*>(pView)->GetDocument())
        {
            switch (pPopupMenu->GetMenuItemID(0))
            {
            case ID_FIRST_SUBTITLE_DUMMY:
                {
                    pPopupMenu->DeleteMenu(0, MF_BYPOSITION);
                    int id = ID_FIRST_SUBTITLE;
                    for (auto& item : pDoc->getFrameDecoder()->listSubtitles())
                    {
                        pPopupMenu->AppendMenu(MF_STRING, id++, CA2T(item.c_str(), CP_UTF8));
                    }
                }
                break;
            case ID_TRACK1_DUMMY:
                {
                    pPopupMenu->DeleteMenu(0, MF_BYPOSITION);
                    int id = ID_TRACK1;
                    const int nTracks = pDoc->getFrameDecoder()->getNumAudioTracks();
                    for (int i = 1; i <= nTracks; ++i)
                    {
                        CString name;
                        name.Format(_T("Track %d"), i);
                        pPopupMenu->AppendMenu(MF_STRING, id++, name);
                    }
                }
                break;
            }
        }
    }
}
