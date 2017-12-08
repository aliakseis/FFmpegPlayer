
// MainFrm.cpp : implementation of the CMainFrame class
//

#include "stdafx.h"
#include "Player.h"

#include "MainFrm.h"
#include "PlayerDoc.h"
#include "IEraseableArea.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

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

} // namespace

// CMainFrame

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWndEx)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWndEx)
    ON_WM_CREATE()
    ON_COMMAND(IDC_FULL_SCREEN, &CMainFrame::OnFullScreen)
    ON_WM_ERASEBKGND()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_NCPAINT()
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
    }

    return result;
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

    if (!m_wndStatusBar.Create(this))
    {
        TRACE0("Failed to create status bar\n");
        return -1;      // fail to create
    }
    m_wndStatusBar.SetIndicators(indicators, sizeof(indicators)/sizeof(UINT));

    if (!m_wndPlayerControl.Create(
        this, 
        IDD_DIALOGBAR_PLAYER_CONTROL, 
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_LEFT | CBRS_FLOAT_MULTI,
        IDD_DIALOGBAR_PLAYER_CONTROL))
    {
        TRACE0("Failed to create dialog bar\n");
        return -1;      // fail to create
    }

    // TODO: Delete these three lines if you don't want the toolbar to be dockable
    m_wndMenuBar.EnableDocking(CBRS_ALIGN_ANY);
    //m_wndToolBar.EnableDocking(CBRS_ALIGN_TOP);
    m_wndPlayerControl.EnableDocking(CBRS_ALIGN_BOTTOM);

    CString strPlayerControl;
    VERIFY(strPlayerControl.LoadString(IDS_PLAYER_CONTROL));
    m_wndPlayerControl.SetWindowText(strPlayerControl);

    EnableDocking(CBRS_ALIGN_ANY);
    DockPane(&m_wndMenuBar);
    //DockPane(&m_wndToolBar, AFX_IDW_DOCKBAR_TOP);
    DockPane(&m_wndPlayerControl, AFX_IDW_DOCKBAR_BOTTOM);

    // Enable toolbar and docking window menu replacement
    CString strCustomize;
    VERIFY(strCustomize.LoadString(IDS_TOOLBAR_CUSTOMIZE));
    //m_wndToolBar.EnableCustomizeButton(TRUE, ID_VIEW_CUSTOMIZE, strCustomize);
    EnablePaneMenu(TRUE, ID_VIEW_CUSTOMIZE, strCustomize, ID_VIEW_TOOLBAR);

    EnableFullScreenMode(IDC_FULL_SCREEN);
    EnableFullScreenMainMenu(FALSE);

    return 0;
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
    ShowFullScreen();
    if (CMFCToolBar* toolBar = static_cast<FullScreenMgrAccessor&>(m_Impl).GetFullScreenBar())
    {
        if (auto pFrame = toolBar->GetParentMiniFrame())
        {
            pFrame->ShowWindow(SW_HIDE);
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
        ModifyStyle(0, WS_OVERLAPPEDWINDOW, 0);

    m_bFullScreen = IsFullScreen();
}


void CMainFrame::OnNcPaint()
{
    if (!IsFullScreen())
        Default();
}
