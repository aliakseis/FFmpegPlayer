// DialogBarPlayerControl.cpp : implementation file
//

#include "stdafx.h"
#include "Player.h"
#include "DialogBarPlayerControl.h"
#include "Theme.h"

#include "MainFrm.h"
#include "PlayerDoc.h"
#include "MakeDelegate.h"
#include "SecondsToString.h"

#include <algorithm>
#include <string>
#include <vector>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

enum { RANGE_MAX = 0x7FFF };

enum { WM_SET_TIME = WM_USER + 101 };

namespace {


HICON CreateNegativeIcon(HICON hIcon)
{
    if (!hIcon)
        return nullptr;

    ICONINFO ii{};
    if (!GetIconInfo(hIcon, &ii))
        return nullptr;

    BITMAP bm{};
    GetObject(ii.hbmColor, sizeof(bm), &bm);

    const int width = bm.bmWidth;
    const int height = bm.bmHeight;

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = width;
    bi.bV5Height = -height; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(nullptr);

    void* bits = nullptr;
    HBITMAP colorBmp = CreateDIBSection(
        hdc,
        reinterpret_cast<BITMAPINFO*>(&bi),
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0);

    if (!colorBmp)
    {
        ReleaseDC(nullptr, hdc);
        return nullptr;
    }

    HBITMAP maskBmp = CreateBitmap(width, height, 1, 1, nullptr);

    HDC srcDC = CreateCompatibleDC(hdc);
    HDC dstDC = CreateCompatibleDC(hdc);

    HBITMAP oldSrc = nullptr;
    HBITMAP oldDst = nullptr;

    ICONINFO srcInfo{};
    GetIconInfo(hIcon, &srcInfo);

    oldSrc = (HBITMAP)SelectObject(srcDC, colorBmp);
    oldDst = (HBITMAP)SelectObject(dstDC, srcInfo.hbmColor);

    BitBlt(srcDC, 0, 0, width, height, dstDC, 0, 0, SRCCOPY);

    auto* pixels = static_cast<DWORD*>(bits);

    for (int i = 0; i < width * height; ++i)
    {
        BYTE b = (pixels[i] >> 0) & 0xFF;
        BYTE g = (pixels[i] >> 8) & 0xFF;
        BYTE r = (pixels[i] >> 16) & 0xFF;
        BYTE a = (pixels[i] >> 24) & 0xFF;

        r = 255 - r;
        g = 255 - g;
        b = 255 - b;

        pixels[i] =
            (DWORD(a) << 24) |
            (DWORD(r) << 16) |
            (DWORD(g) << 8) |
            DWORD(b);
    }

    SelectObject(srcDC, oldSrc);
    SelectObject(dstDC, oldDst);

    DeleteDC(srcDC);
    DeleteDC(dstDC);

    DeleteObject(srcInfo.hbmColor);
    DeleteObject(srcInfo.hbmMask);

    ICONINFO newInfo{};
    newInfo.fIcon = TRUE;
    newInfo.hbmColor = colorBmp;
    newInfo.hbmMask = maskBmp;

    HICON result = CreateIconIndirect(&newInfo);

    DeleteObject(colorBmp);
    DeleteObject(maskBmp);

    ReleaseDC(nullptr, hdc);

    return result;
}

HICON LoadIcon(int idr, int iconSize)
{
    return (HICON) LoadImage(
        AfxGetApp()->m_hInstance,
        MAKEINTRESOURCE(idr),
        IMAGE_ICON,
        iconSize, iconSize, // use actual size
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
, m_savedVolume(0)
, m_oldTotalTime(-1) // unset
, m_oldCurrentTime(-1) // unset
, m_tracking(false)
, m_selStart(0)
, m_selEnd(RANGE_MAX)
{
    CDialogTemplate dlgtemplate;
    if (dlgtemplate.Load(MAKEINTRESOURCE(IDD)))
    {
        CSize size;
        dlgtemplate.GetSizeInPixels(&size);
        SetMinSize(size);
    }
}

CDialogBarPlayerControl::~CDialogBarPlayerControl()
{
    onDocDetaching();
}

void CDialogBarPlayerControl::onDocDetaching()
{
    if (m_pDoc)
    {
        m_pDoc->framePositionChanged.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onFramePositionChanged, this));
        m_pDoc->totalTimeUpdated.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onTotalTimeUpdated, this));
        m_pDoc->currentTimeUpdated.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onCurrentTimeUpdated, this));

        m_pDoc->rangeStartTimeChanged.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onRangeStartTimeChanged, this));
        m_pDoc->rangeEndTimeChanged.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onRangeEndTimeChanged, this));

        m_pDoc->onDestructing.disconnect(MAKE_DELEGATE(&CDialogBarPlayerControl::onDocDetaching, this));

        m_pDoc = nullptr;
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
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_DRAWITEM()
    ON_WM_HSCROLL()
    ON_UPDATE_COMMAND_UI(IDC_PLAY_PAUSE, &CDialogBarPlayerControl::OnUpdatePlayPause)
    ON_UPDATE_COMMAND_UI(IDC_AUDIO_ON_OFF, &CDialogBarPlayerControl::OnUpdateAudioOnOff)
    ON_BN_CLICKED(IDC_PLAY_PAUSE, &CDialogBarPlayerControl::OnClickedPlayPause)
    ON_BN_CLICKED(IDC_AUDIO_ON_OFF, &CDialogBarPlayerControl::OnClickedAudioOnOff)
    ON_BN_DOUBLECLICKED(IDC_PLAY_PAUSE, &CDialogBarPlayerControl::OnClickedPlayPause)
    ON_BN_DOUBLECLICKED(IDC_AUDIO_ON_OFF, &CDialogBarPlayerControl::OnClickedAudioOnOff)
    ON_MESSAGE(WM_SET_TIME, &CDialogBarPlayerControl::OnSetTime)
    ON_MESSAGE(WM_INITDIALOG, &CDialogBarPlayerControl::HandleInitDialog)
    ON_UPDATE_COMMAND_UI(IDC_FRAME_STEP, &CDialogBarPlayerControl::OnUpdateFrameStep)
    ON_UPDATE_COMMAND_UI(IDC_VOLUME_SLIDER, &CDialogBarPlayerControl::OnUpdateVolumeSlider)
    ON_UPDATE_COMMAND_UI(IDC_CURRENT_TIME, &CDialogBarPlayerControl::OnUpdateCurrentTime)
    END_MESSAGE_MAP()

HBRUSH CDialogBarPlayerControl::OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor)
{
    return CThemeManager::OnCtlColor(dc, wnd, ctlColor);
}

BOOL CDialogBarPlayerControl::OnEraseBkgnd(CDC* dc)
{
    if (!CThemeManager::IsDark())
        return __super::OnEraseBkgnd(dc);
    CRect rc;
    GetClientRect(&rc);
    dc->FillSolidRect(rc, CThemeManager::GetColors().window);
    return TRUE;
}

void CDialogBarPlayerControl::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT drawItem)
{
    UNREFERENCED_PARAMETER(nIDCtl);
    HICON icon = nullptr;
    if (drawItem)
    {
        const bool dark = CThemeManager::IsDark();

        switch (drawItem->CtlID)
        {
        case IDC_PLAY_PAUSE:  icon = m_bPaused ? (dark? m_hPlayNeg : m_hPlay) 
            : (dark ? m_hPauseNeg : m_hPause); 
            break;
        case IDC_AUDIO_ON_OFF: icon = m_bAudioOn ? (dark ? m_hAudioNeg  :m_hAudio)
            : (dark ? m_hAudioOffNeg : m_hAudioOff); 
            break;
        case IDC_FULL_SCREEN: icon = dark ? m_hFullScreenNeg : m_hFullScreen; break;
        }
    }
    CThemeManager::DrawButton(this, drawItem, false, false, icon);
}



// CDialogBarPlayerControl message handlers

LRESULT CDialogBarPlayerControl::HandleInitDialog(WPARAM wParam, LPARAM lParam)
{
    __super::HandleInitDialog(wParam, lParam);

    CRect btnRect;
    GetDlgItem(IDC_PLAY_PAUSE)->GetClientRect(btnRect);
    const int iconSizeLimit = min(btnRect.Width() - 2 * GetSystemMetrics(SM_CXBORDER),
        btnRect.Height() - 2 * GetSystemMetrics(SM_CYBORDER)) * 4 / 5;

    const int iconSize = min(iconSizeLimit & ((iconSizeLimit > 32)? ~15 : ~7), 48);

    m_hPlay = LoadIcon(IDI_PLAY, iconSize);
    m_hPlayNeg = CreateNegativeIcon(m_hPlay);
    m_hPause = LoadIcon(IDI_PAUSE, iconSize);
    m_hPauseNeg = CreateNegativeIcon(m_hPause);
    m_hAudio = LoadIcon(IDI_AUDIO, iconSize);
    m_hAudioNeg = CreateNegativeIcon(m_hAudio);
    m_hAudioOff = LoadIcon(IDI_AUDIO_OFF, iconSize);
    m_hAudioOffNeg = CreateNegativeIcon(m_hAudioOff);
    m_hFullScreen = LoadIcon(IDI_FULL_SCREEN, iconSize);
    m_hFullScreenNeg = CreateNegativeIcon(m_hFullScreen);

    //static_cast<CButton*>(GetDlgItem(IDC_PLAY_PAUSE))->SetIcon(m_hPlay);
    //static_cast<CButton*>(GetDlgItem(IDC_AUDIO_ON_OFF))->SetIcon(m_hAudio);
    //static_cast<CButton*>(GetDlgItem(IDC_FULL_SCREEN))->SetIcon(m_hFullScreen);

    m_progressSlider.SetRange(0, RANGE_MAX);
    m_progressSlider.SetPageSize(0);
    m_progressSlider.ModifyStyle(0, TBS_ENABLESELRANGE);
    m_volumeSlider.SetRange(0, RANGE_MAX);
    m_volumeSlider.SetPos(RANGE_MAX);
    m_volumeSlider.SetPageSize(0);

    ::SetWindowTheme(GetDlgItem(IDC_FRAME_STEP)->GetSafeHwnd(), L"", L"");

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

    m_pDoc->onDestructing.connect(MAKE_DELEGATE(&CDialogBarPlayerControl::onDocDetaching, this));
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

enum { MIN_SEL_WIDTH = 50 };

void CDialogBarPlayerControl::onRangeStartTimeChanged(long long frame, long long total)
{
    ASSERT(total >= 0);
    const int pos = (total > 0) ? int((frame * (RANGE_MAX - MIN_SEL_WIDTH)) / total) : 0;
    m_selStart = pos;
    m_progressSlider.SendNotifyMessage(TBM_SETSEL, TRUE, MAKELPARAM(pos, m_selEnd));
}

void CDialogBarPlayerControl::onRangeEndTimeChanged(long long frame, long long total)
{
    ASSERT(total >= 0);
    const int pos = (total > 0) ? int((frame * (RANGE_MAX - MIN_SEL_WIDTH)) / total) + MIN_SEL_WIDTH : 0;
    m_selEnd = pos;
    m_progressSlider.SendNotifyMessage(TBM_SETSEL, TRUE, MAKELPARAM(m_selStart, pos));
}

void CDialogBarPlayerControl::onTotalTimeUpdated(double secs)
{
    int totalTime = int(secs * 1000);
    if (totalTime == m_oldTotalTime)
        return;

    m_oldTotalTime = totalTime;

    SendNotifyMessage(WM_SET_TIME, IDC_TOTAL_TIME, totalTime);
}

void CDialogBarPlayerControl::onCurrentTimeUpdated(double secs)
{
    int currentTime = int(secs * 1000);
    const bool notify = (currentTime / 1000 != m_oldCurrentTime / 1000) ||
                        m_pDoc && m_pDoc->isPaused() && currentTime != m_oldCurrentTime;

    m_oldCurrentTime = currentTime;

    if (notify)
        SendNotifyMessage(WM_SET_TIME, IDC_CURRENT_TIME, currentTime);
}

LRESULT CDialogBarPlayerControl::OnSetTime(WPARAM wParam, LPARAM lParam)
{
    const bool milli = wParam == IDC_CURRENT_TIME && m_pDoc && m_pDoc->isPaused();
    SetDlgItemText(wParam, secondsToString(lParam, milli).c_str());
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
                case SB_LINELEFT:
                    if (m_pDoc->isPaused())
                    {
                        m_pDoc->prevFrame();
                    }
                    break;
                case SB_LINERIGHT:
                    if (m_pDoc->isPaused())
                    {
                        m_pDoc->nextFrame();
                    }
                    break;

                case SB_PAGELEFT: 
                case SB_PAGERIGHT:
                    m_pDoc->seekByPercent(GetValueByMouseClick(this, &m_progressSlider));
                    break;

                case SB_THUMBTRACK:
                    m_pDoc->seekByPercent(m_progressSlider.GetPos() / double(RANGE_MAX));
                    m_tracking = true;
                    break;

                case SB_RIGHT:
                    m_pDoc->seekToEnd();
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
            const bool paused = m_pDoc->isPaused();
            if (paused != m_bPaused)
            {
                m_bPaused = paused;
                static_cast<CButton*>(pCmdUI->m_pOther)->Invalidate();
                    //SetIcon(paused ? m_hPlay : m_hPause);
            }
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
            const bool audioOn = m_volumeSlider.GetPos() > 0;
            if (audioOn != m_bAudioOn)
            {
                m_bAudioOn = audioOn;
                static_cast<CButton*>(pCmdUI->m_pOther)->Invalidate();
                    //SetIcon(audioOn ? m_hAudio : m_hAudioOff);
            }
        }
    }
}

void CDialogBarPlayerControl::OnClickedPlayPause()
{
    if (m_pDoc)
    {
        if (m_pDoc->isPaused() && IsDlgButtonChecked(IDC_FRAME_STEP))
        {
            if (GetKeyState(VK_SHIFT) < 0)
                m_pDoc->prevFrame();
            else
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

void CDialogBarPlayerControl::OnUpdateCurrentTime(CCmdUI* pCmdUI)
{
    if (pCmdUI->m_pOther && m_pDoc && m_pDoc->isPaused())
    {
        pCmdUI->m_pOther->SetWindowText(secondsToString(m_oldCurrentTime, true).c_str());
    }
}
