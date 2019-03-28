
// MainFrm.h : interface of the CMainFrame class
//

#pragma once

#include "DialogBarPlayerControl.h"
#include "DialogBarRange.h"

struct ITaskbarList3;

class CMainFrame : public CFrameWndEx
{
    
protected: // create from serialization only
    CMainFrame();
    DECLARE_DYNCREATE(CMainFrame)

// Attributes
public:

// Operations
private:
    void onPauseResume(bool paused);


// Overrides
public:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override;

    BOOL Create(LPCTSTR lpszClassName,
        LPCTSTR lpszWindowName,
        DWORD dwStyle = WS_OVERLAPPEDWINDOW,
        const RECT& rect = rectDefault,
        CWnd* pParentWnd = NULL,        // != NULL for popups
        LPCTSTR lpszMenuName = NULL,
        DWORD dwExStyle = 0,
        CCreateContext* pContext = NULL) override;

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

// Implementation
public:
    virtual ~CMainFrame();
#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif

protected:  // control bar embedded members
    CMFCMenuBar       m_wndMenuBar;
    //CMFCToolBar          m_wndToolBar;
    CMFCStatusBar        m_wndStatusBar;
    CDialogBarPlayerControl m_wndPlayerControl;
    CDialogBarRange m_wndRange;

    BOOL            m_bFullScreen;

    CComPtr<ITaskbarList3>  m_pTaskbarList;

    HICON m_hPlay;
    HICON m_hPause;

// Generated message map functions
protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnClose();
    afx_msg LRESULT CreateThumbnailToolbar(WPARAM wParam, LPARAM lParam);
    afx_msg void OnFullScreen();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
    afx_msg void OnNcPaint();
    afx_msg UINT OnPowerBroadcast(UINT nPowerEvent, LPARAM nEventData);
    DECLARE_MESSAGE_MAP()
};


