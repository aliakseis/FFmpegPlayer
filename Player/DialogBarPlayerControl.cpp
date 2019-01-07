// DialogBarPlayerControl.cpp : implementation file
//

#include "stdafx.h"
#include "Player.h"
#include "DialogBarPlayerControl.h"

#include "MainFrm.h"
#include "PlayerDoc.h"
#include "MakeDelegate.h"

#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>


using std::setfill;
using std::setw;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

enum { RANGE_MAX = 0x7FFF };

enum { ICON_SIZE = 24 };

enum { WM_SET_TIME = WM_USER + 101 };

namespace {

std::basic_string<TCHAR> secondsToString(int seconds)
{
    std::basic_ostringstream<TCHAR> buffer;
    int s = seconds % 60;
    int m = (seconds / 60) % 60;
    int h = seconds / 3600;

    if (h > 0)
    { 
        buffer << h << ':';
    }
    buffer << setfill(_T('0')) << setw(2) << m << ':' << setfill(_T('0')) << setw(2) << s;

    return buffer.str();
}

HICON LoadIcon(int idr)
{
    return (HICON) LoadImage(
        AfxGetApp()->m_hInstance,
        MAKEINTRESOURCE(idr),
        IMAGE_ICON,
        ICON_SIZE, ICON_SIZE, // use actual size
        LR_DEFAULTCOLOR);
}

double GetValueByMouseClick(CWnd* pDlg, CSliderCtrl* pSliderCtrl)
{
    CRect   rectClient, rectChannel, rectThumb;
    pDlg->GetClientRect(rectClient);
    pSliderCtrl->GetChannelRect(rectChannel);
    pSliderCtrl->GetThumbRect(rectThumb);
    rectChannel.DeflateRect(rectThumb.Width() / 2, 0);
    CPoint mousePt(AfxGetCurrentMessage()->pt);
    pSliderCtrl->ScreenToClient(&mousePt);
    return std::clamp(
        double(mousePt.x - rectClient.left - rectChannel.left) /
        (rectChannel.right - rectChannel.left),
        0., 1.);
}

}


// CDialogBarPlayerControl

IMPLEMENT_DYNAMIC(CDialogBarPlayerControl, CPaneDialog)

CDialogBarPlayerControl::CDialogBarPlayerControl()
: m_pDoc(nullptr)
, m_hPlay(NULL)
, m_hPause(NULL)
, m_hAudio(NULL)
, m_hAudioOff(NULL)
, m_hFullScreen(NULL)
, m_savedVolume(0)
, m_oldTotalTime(-1) // unset
, m_oldCurrentTime(-1) // unset
, m_tracking(false)
{
    SetMinSize(CSize(500, 40));
}

CDialogBarPlayerControl::~CDialogBarPlayerControl()
{
    if (m_pDoc)
    {
        m_pDoc->framePositionChanged.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onFramePositionChanged, this));
        m_pDoc->totalTimeUpdated.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onTotalTimeUpdated, this));
        m_pDoc->currentTimeUpdated.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onCurrentTimeUpdated, this));

        m_pDoc->rangeStartTimeChanged.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onRangeStartTimeChanged, this));
        m_pDoc->rangeEndTimeChanged.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onRangeEndTimeChanged, this));
    }
}

void CDialogBarPlayerControl::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CDialogBarPlayerControl)
    // NOTE: the ClassWizard will add DDX and DDV calls here
    //}}AFX_DATA_MAP
    DDX_Control(pDX, IDC_PROGRESS_SLIDER, m_progressSlider);
    DDX_Control(pDX, IDC_VOLUME_SLIDER, m_volumeSlider);
}


BEGIN_MESSAGE_MAP(CDialogBarPlayerControl, CPaneDialog)
    ON_WM_HSCROLL()
    ON_UPDATE_COMMAND_UI(IDC_PLAY_PAUSE, &CDialogBarPlayerControl::OnUpdatePlayPause)
    ON_UPDATE_COMMAND_UI(IDC_AUDIO_ON_OFF, &CDialogBarPlayerControl::OnUpdateAudioOnOff)
    ON_BN_CLICKED(IDC_PLAY_PAUSE, &CDialogBarPlayerControl::OnClickedPlayPause)
    ON_BN_CLICKED(IDC_AUDIO_ON_OFF, &CDialogBarPlayerControl::OnClickedAudioOnOff)
    ON_MESSAGE(WM_SET_TIME, &CDialogBarPlayerControl::OnSetTime)
    ON_MESSAGE(WM_INITDIALOG, &CDialogBarPlayerControl::HandleInitDialog)
    ON_UPDATE_COMMAND_UI(IDC_FRAME_STEP, &CDialogBarPlayerControl::OnUpdateFrameStep)
    ON_UPDATE_COMMAND_UI(IDC_VOLUME_SLIDER, &CDialogBarPlayerControl::OnUpdateVolumeSlider)
END_MESSAGE_MAP()



// CDialogBarPlayerControl message handlers

LRESULT CDialogBarPlayerControl::HandleInitDialog(WPARAM wParam, LPARAM lParam)
{
    __super::HandleInitDialog(wParam, lParam);

    m_hPlay = LoadIcon(IDI_PLAY);
    m_hPause = LoadIcon(IDI_PAUSE);
    m_hAudio = LoadIcon(IDI_AUDIO);
    m_hAudioOff = LoadIcon(IDI_AUDIO_OFF);
    m_hFullScreen = LoadIcon(IDI_FULL_SCREEN);

    static_cast<CButton*>(GetDlgItem(IDC_PLAY_PAUSE))->SetIcon(m_hPlay);
    static_cast<CButton*>(GetDlgItem(IDC_AUDIO_ON_OFF))->SetIcon(m_hAudio);
    static_cast<CButton*>(GetDlgItem(IDC_FULL_SCREEN))->SetIcon(m_hFullScreen);

    m_progressSlider.SetRange(0, RANGE_MAX);
    m_progressSlider.SetPageSize(0);
    m_progressSlider.ModifyStyle(0, TBS_ENABLESELRANGE);
    m_volumeSlider.SetRange(0, RANGE_MAX);
    m_volumeSlider.SetPos(RANGE_MAX);
    m_volumeSlider.SetPageSize(0);

    return TRUE;
}

void CDialogBarPlayerControl::setDocument(CPlayerDoc* pDoc)
{
    ASSERT(!m_pDoc);
    m_pDoc = pDoc;

    m_volumeSlider.SetPos(int(RANGE_MAX * pDoc->soundVolume()));

    m_pDoc->framePositionChanged.connect(MAKE_DELEGATE(&CDialogBarPlayerControl::onFramePositionChanged, this));
    m_pDoc->totalTimeUpdated.connect(MAKE_DELEGATE(&CDialogBarPlayerControl::onTotalTimeUpdated, this));
    m_pDoc->currentTimeUpdated.connect(MAKE_DELEGATE(&CDialogBarPlayerControl::onCurrentTimeUpdated, this));

    m_pDoc->rangeStartTimeChanged.connect(MAKE_DELEGATE(&CDialogBarPlayerControl::onRangeStartTimeChanged, this));
    m_pDoc->rangeEndTimeChanged.connect(MAKE_DELEGATE(&CDialogBarPlayerControl::onRangeEndTimeChanged, this));
}

void CDialogBarPlayerControl::onFramePositionChanged(long long frame, long long total)
{
    if (!m_tracking)
    {
        ASSERT(total >= 0);
        const int pos = (total > 0)? int((frame * RANGE_MAX) / total) : 0;
        m_progressSlider.SendNotifyMessage(TBM_SETPOS, TRUE, pos);
    }
}

void CDialogBarPlayerControl::onRangeStartTimeChanged(long long frame, long long total)
{
    ASSERT(total >= 0);
    const int pos = (total > 0) ? int((frame * RANGE_MAX) / total) : 0;
    m_progressSlider.SendNotifyMessage(TBM_SETSELSTART, TRUE, pos);
}

void CDialogBarPlayerControl::onRangeEndTimeChanged(long long frame, long long total)
{
    ASSERT(total >= 0);
    const int pos = (total > 0) ? int((frame * RANGE_MAX) / total) : 0;
    m_progressSlider.SendNotifyMessage(TBM_SETSELEND, TRUE, pos);
}

void CDialogBarPlayerControl::onTotalTimeUpdated(double secs)
{
    int totalTime = int(secs + 0.5);
    if (totalTime == m_oldTotalTime)
        return;

    m_oldTotalTime = totalTime;

    SendNotifyMessage(WM_SET_TIME, IDC_TOTAL_TIME, totalTime);
}

void CDialogBarPlayerControl::onCurrentTimeUpdated(double secs)
{
    int currentTime = int(secs + 0.5);
    if (currentTime == m_oldCurrentTime)
        return;

    m_oldCurrentTime = currentTime;

    SendNotifyMessage(WM_SET_TIME, IDC_CURRENT_TIME, currentTime);
}

LRESULT CDialogBarPlayerControl::OnSetTime(WPARAM wParam, LPARAM lParam)
{
    SetDlgItemText(wParam, secondsToString(lParam).c_str());
    return 0;
}

void CDialogBarPlayerControl::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (m_pDoc)
    {
        if (static_cast<CWnd*>(pScrollBar) == &m_progressSlider)
        {
            switch (nSBCode)
            {
                case SB_PAGELEFT: 
                case SB_PAGERIGHT:
                    m_pDoc->seekByPercent(GetValueByMouseClick(this, &m_progressSlider));
                    break;

                case SB_THUMBTRACK:
                    m_pDoc->seekByPercent(m_progressSlider.GetPos() / double(RANGE_MAX));
                    m_tracking = true;
                    break;

                case SB_ENDSCROLL:
                    m_tracking = false;
                    break;
            }
        }
        else if (static_cast<CWnd*>(pScrollBar) == &m_volumeSlider)
        {
            if (nSBCode == SB_PAGELEFT || nSBCode == SB_PAGERIGHT)
            {
                const double valueByMouseClick = GetValueByMouseClick(this, &m_volumeSlider);
                m_volumeSlider.SetPos(int(valueByMouseClick * RANGE_MAX));
                m_pDoc->setVolume(valueByMouseClick);
            }
            else
            {
                m_pDoc->setVolume(m_volumeSlider.GetPos() / double(RANGE_MAX));
            }
        }
    }

    __super::OnHScroll(nSBCode, nPos, pScrollBar);
}


void CDialogBarPlayerControl::OnUpdatePlayPause(CCmdUI *pCmdUI)
{
    if (m_pDoc)
    {
        pCmdUI->Enable(m_pDoc->isPlaying());
        if (pCmdUI->m_pOther)
        {
            static_cast<CButton*>(pCmdUI->m_pOther)->SetIcon(
                m_pDoc->isPaused() ? m_hPlay : m_hPause);
        }
    }
}

void CDialogBarPlayerControl::OnUpdateAudioOnOff(CCmdUI *pCmdUI)
{
    if (m_pDoc)
    {
        pCmdUI->Enable(m_pDoc->isPlaying());
        if (pCmdUI->m_pOther)
        {
            static_cast<CButton*>(pCmdUI->m_pOther)->SetIcon(
                (m_volumeSlider.GetPos() > 0) ? m_hAudio : m_hAudioOff);
        }
    }

}

void CDialogBarPlayerControl::OnClickedPlayPause()
{
    if (m_pDoc)
    {
        if (m_pDoc->isPaused() && IsDlgButtonChecked(IDC_FRAME_STEP))
        {
            m_pDoc->nextFrame();
        }
        else
        {
            m_pDoc->pauseResume();
        }
    }
}


void CDialogBarPlayerControl::OnClickedAudioOnOff()
{
    int newVolume;
    if (m_savedVolume)
    {
        newVolume = m_savedVolume;
        m_savedVolume = 0;
    }
    else
    {
        m_savedVolume = m_volumeSlider.GetPos();
        newVolume = 0;
    }

    m_volumeSlider.SetPos(newVolume);
    m_pDoc->setVolume(newVolume / double(RANGE_MAX));
}


void CDialogBarPlayerControl::OnUpdateFrameStep(CCmdUI *pCmdUI)
{
    if (pCmdUI->m_pOther)
    {
        pCmdUI->m_pOther->ShowWindow(
            (m_pDoc && m_pDoc->isPaused()) ? SW_SHOWNA : SW_HIDE);
    }
}


void CDialogBarPlayerControl::OnUpdateVolumeSlider(CCmdUI *pCmdUI)
{
    if (pCmdUI->m_pOther)
    {
        pCmdUI->m_pOther->ShowWindow(
            (m_pDoc && m_pDoc->isPaused()) ? SW_HIDE : SW_SHOWNA);
    }
}
