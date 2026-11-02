#pragma once

#include "Theme.h"

#include <memory>

class CPlayerDoc;

struct IconDeleter
{
    void operator()(HICON h) const noexcept
    {
        if (h)
            VERIFY(DestroyIcon(h));
    }
};

using UniqueIcon = std::unique_ptr<
    std::remove_pointer_t<HICON>,
    IconDeleter>;

// CDialogBarPlayerControl

class CDialogBarPlayerControl
    : public CPaneDialog
{
    DECLARE_DYNAMIC(CDialogBarPlayerControl)

public:
    CDialogBarPlayerControl();
    virtual ~CDialogBarPlayerControl();

    // Dialog Data
    //{{AFX_DATA(CDialogBarPlayerControl)
    enum { IDD = IDD_DIALOGBAR_PLAYER_CONTROL };
    // NOTE: the ClassWizard will add data members here
    //}}AFX_DATA

    void setDocument(CPlayerDoc* pDoc);

    // Overrides
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CDialogBarPlayerControl)
protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    //}}AFX_VIRTUAL

    int GetCaptionHeight() const override { return 0; }

    void onFramePositionChanged(long long frame, long long total);
    void onTotalTimeUpdated(double secs);
    void onCurrentTimeUpdated(double secs);

    void onRangeStartTimeChanged(long long frame, long long total);
    void onRangeEndTimeChanged(long long frame, long long total);

    void onDocDetaching();

protected:
    DECLARE_MESSAGE_MAP()
    afx_msg LRESULT HandleInitDialog(WPARAM wParam, LPARAM lParam);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor);
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT drawItem);
    afx_msg LRESULT OnSetTime(WPARAM wParam, LPARAM lParam);
public:

private:
    CPlayerDoc* m_pDoc;
    UniqueIcon m_hPlay;
    UniqueIcon m_hPause;
    UniqueIcon m_hAudio;
    UniqueIcon m_hAudioOff;
    UniqueIcon m_hFullScreen;
    UniqueIcon m_hPlayNeg;
    UniqueIcon m_hPauseNeg;
    UniqueIcon m_hAudioNeg;
    UniqueIcon m_hAudioOffNeg;
    UniqueIcon m_hFullScreenNeg;
    bool m_bPaused = true;
    bool m_bAudioOn = true;
    int m_savedVolume;
    volatile int m_oldTotalTime;
    volatile int m_oldCurrentTime;
    bool m_tracking;

    int m_selStart;
    int m_selEnd;

public:
    CThemedSliderCtrl m_progressSlider;
    CThemedSliderCtrl m_volumeSlider;
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnUpdatePlayPause(CCmdUI *pCmdUI);
    afx_msg void OnUpdateAudioOnOff(CCmdUI *pCmdUI);
    afx_msg void OnClickedPlayPause();
    afx_msg void OnClickedAudioOnOff();
    afx_msg void OnUpdateFrameStep(CCmdUI *pCmdUI);
    afx_msg void OnUpdateVolumeSlider(CCmdUI *pCmdUI);
    afx_msg void OnUpdateCurrentTime(CCmdUI *pCmdUI);
};


