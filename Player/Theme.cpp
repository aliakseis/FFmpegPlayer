#include "stdafx.h"
#include "Theme.h"
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "uxtheme.lib")

namespace
{
    bool g_dark = false;
    CBrush g_windowBrush;
    CBrush g_controlBrush;
    bool g_brushesValid = false;

    const CThemeManager::Colors kLight =
    {
        GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_WINDOWTEXT),
        GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT),
        GetSysColor(COLOR_GRAYTEXT), GetSysColor(COLOR_3DSHADOW),
        GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT),
        GetSysColor(COLOR_3DLIGHT), GetSysColor(COLOR_3DSHADOW)
    };

    const CThemeManager::Colors kDark =
    {
        RGB(37, 38, 41), RGB(226, 228, 232),
        RGB(45, 47, 51), RGB(230, 232, 236),
        RGB(133, 137, 145), RGB(67, 70, 76),
        RGB(55, 91, 130), RGB(245, 247, 250),
        RGB(54, 59, 66), RGB(43, 67, 91)
    };

    void DrawCheckMark(CDC& dc, const CRect& rc, COLORREF color)
    {
        CPen pen(PS_SOLID, 2, color);
        CPen* oldPen = dc.SelectObject(&pen);
        dc.MoveTo(rc.left + 2, rc.CenterPoint().y);
        dc.LineTo(rc.left + rc.Width() / 2 - 1, rc.bottom - 3);
        dc.LineTo(rc.right - 2, rc.top + 2);
        dc.SelectObject(oldPen);
    }
}

IMPLEMENT_DYNAMIC(CThemedDialog, CDialogEx)
IMPLEMENT_DYNCREATE(CPlayerVisualManager, CMFCVisualManagerOffice2007)

void CPlayerVisualManager::OnFillBarBackground(CDC* dc, CBasePane* bar,
    CRect client, CRect clip, BOOL nonClient)
{
    if (!CThemeManager::IsDark())
    {
        __super::OnFillBarBackground(dc, bar, client, clip, nonClient);
        return;
    }
    if (dc)
        dc->FillSolidRect(client, CThemeManager::GetColors().window);
}

void CPlayerVisualManager::OnFillPopupWindowBackground(CDC* dc, CRect rect)
{
    if (!CThemeManager::IsDark())
    {
        __super::OnFillPopupWindowBackground(dc, rect);
        return;
    }
    if (dc)
        dc->FillSolidRect(rect, CThemeManager::GetColors().window);
}

void CPlayerVisualManager::OnDrawMenuBorder(CDC* dc, CMFCPopupMenu* menu, CRect rect)
{
    if (!CThemeManager::IsDark())
    {
        __super::OnDrawMenuBorder(dc, menu, rect);
        return;
    }
    if (dc)
    {
        CBrush border(CThemeManager::GetColors().border);
        dc->FrameRect(rect, &border);
    }
}

void CPlayerVisualManager::OnHighlightMenuItem(CDC* dc,
    CMFCToolBarMenuButton* button, CRect rect, COLORREF& textColor)
{
    if (!CThemeManager::IsDark())
    {
        __super::OnHighlightMenuItem(dc, button, rect, textColor);
        return;
    }
    const CThemeManager::Colors& c = CThemeManager::GetColors();
    if (dc)
    {
        dc->FillSolidRect(rect, c.selection);
        CBrush border(c.border);
        dc->FrameRect(rect, &border);
    }
    textColor = c.selectionText;
}

COLORREF CPlayerVisualManager::GetMenuItemTextColor(
    CMFCToolBarMenuButton* button, BOOL highlighted, BOOL disabled)
{
    if (!CThemeManager::IsDark())
        return __super::GetMenuItemTextColor(button, highlighted, disabled);
    const CThemeManager::Colors& c = CThemeManager::GetColors();
    return disabled ? c.disabledText : (highlighted ? c.selectionText : c.text);
}

COLORREF CPlayerVisualManager::GetHighlightedMenuItemTextColor(
    CMFCToolBarMenuButton* button)
{
    if (!CThemeManager::IsDark())
        return __super::GetHighlightedMenuItemTextColor(button);
    return CThemeManager::GetColors().selectionText;
}

COLORREF CPlayerVisualManager::OnDrawMenuLabel(CDC* dc, CRect rect)
{
    if (!CThemeManager::IsDark())
        return __super::OnDrawMenuLabel(dc, rect);
    if (dc)
        dc->FillSolidRect(rect, CThemeManager::GetColors().window);
    return CThemeManager::GetColors().text;
}

void CThemeManager::SetDark(bool dark)
{
    if (g_dark != dark)
    {
        g_dark = dark;
        g_brushesValid = false;
    }
}

bool CThemeManager::IsDark() { return g_dark; }
const CThemeManager::Colors& CThemeManager::GetColors() { return g_dark ? kDark : kLight; }

void CThemeManager::EnsureBrushes()
{
    if (g_brushesValid)
        return;
    const Colors& c = GetColors();
    g_windowBrush.DeleteObject();
    g_controlBrush.DeleteObject();
    g_windowBrush.CreateSolidBrush(c.window);
    g_controlBrush.CreateSolidBrush(c.control);
    g_brushesValid = true;
}

HBRUSH CThemeManager::GetWindowBrush()
{
    EnsureBrushes();
    return static_cast<HBRUSH>(g_windowBrush.GetSafeHandle());
}

HBRUSH CThemeManager::GetControlBrush()
{
    EnsureBrushes();
    return static_cast<HBRUSH>(g_controlBrush.GetSafeHandle());
}

HBRUSH CThemeManager::OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor)
{
    const Colors& c = GetColors();
    const bool enabled = !wnd || wnd->IsWindowEnabled();
    dc->SetTextColor(enabled ? c.text : c.disabledText);

    switch (ctlColor)
    {
    case CTLCOLOR_EDIT:
    case CTLCOLOR_LISTBOX:
        dc->SetTextColor(enabled ? c.controlText : c.disabledText);
        dc->SetBkColor(c.control);
        dc->SetBkMode(OPAQUE);
        return GetControlBrush();
    case CTLCOLOR_STATIC:
    case CTLCOLOR_BTN:
        dc->SetBkMode(TRANSPARENT);
        dc->SetBkColor(c.window);
        return GetWindowBrush();
    default:
        dc->SetBkColor(c.window);
        dc->SetBkMode(OPAQUE);
        return GetWindowBrush();
    }
}

void CThemeManager::DrawButton(CWnd* parent, LPDRAWITEMSTRUCT dis, bool checkBox, bool checked, HICON icon)
{
    if (!parent || !dis || dis->CtlType != ODT_BUTTON)
        return;

    CDC* dc = CDC::FromHandle(dis->hDC);
    if (!dc)
        return;

    const Colors& c = GetColors();
    const UINT state = dis->itemState;
    const bool disabled = (state & ODS_DISABLED) != 0;
    const bool pressed = (state & ODS_SELECTED) != 0;
    const bool hot = (state & ODS_HOTLIGHT) != 0;
    CRect rc(dis->rcItem);

    CWnd* child = parent->GetDlgItem(static_cast<int>(dis->CtlID));
    if (!child)
        return;

    if (CThemeManager::IsDark())
    {
        if (checkBox)
        {
            dc->FillSolidRect(rc, c.window);
        }
        else
        {
            COLORREF face = c.window;
            if (pressed) face = c.buttonPressed;
            else if (hot) face = c.buttonHot;
            dc->FillSolidRect(rc, face);
            CBrush border(c.border);
            dc->FrameRect(rc, &border);
        }
    }
    else
    {
        // These controls are owner-drawn so the dark theme can paint them.
        // In light mode, delegate their background to the native visual style.
        HTHEME theme = ::OpenThemeData(parent->GetSafeHwnd(), L"BUTTON");
        bool themedBackground = false;
        if (theme)
        {
            int part = checkBox ? BP_CHECKBOX : BP_PUSHBUTTON;
            int stateId = PBS_NORMAL;
            if (checkBox)
            {
                if (disabled) stateId = checked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED;
                else if (pressed) stateId = checked ? CBS_CHECKEDPRESSED : CBS_UNCHECKEDPRESSED;
                else if (hot) stateId = checked ? CBS_CHECKEDHOT : CBS_UNCHECKEDHOT;
                else stateId = checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
            }
            else
            {
                if (disabled) stateId = PBS_DISABLED;
                else if (pressed) stateId = PBS_PRESSED;
                else if (hot) stateId = PBS_HOT;
                else if (state & ODS_DEFAULT) stateId = PBS_DEFAULTED;
            }
            themedBackground = SUCCEEDED(::DrawThemeBackground(theme, dis->hDC, part, stateId, &rc, nullptr));
            ::CloseThemeData(theme);
        }
        if (!themedBackground)
        {
            if (checkBox)
            {
                UINT flags = DFCS_BUTTONCHECK;
                if (checked) flags |= DFCS_CHECKED;
                if (disabled) flags |= DFCS_INACTIVE;
                if (pressed) flags |= DFCS_PUSHED;
                ::DrawFrameControl(dis->hDC, &rc, DFC_BUTTON, flags);
            }
            else
            {
                dc->FillSolidRect(rc, c.window);
                CBrush border(c.border);
                dc->FrameRect(rc, &border);
            }
        }
    }

    if (checkBox)
    {
        const int boxSize = min(12, max(9, rc.Height() - 4));
        CRect box(rc.left + 3, rc.CenterPoint().y - boxSize / 2,
            rc.left + 3 + boxSize, rc.CenterPoint().y + (boxSize + 1) / 2);
        if (CThemeManager::IsDark())
        {
            CBrush boxBrush(c.control);
            CBrush border(c.border);
            dc->FillRect(box, &boxBrush);
            dc->FrameRect(box, &border);
            if (checked)
                DrawCheckMark(*dc, box, disabled ? c.disabledText : c.selection);
        }
        rc.left = box.right + 4;
    }
    else
    {
        if (!icon)
            icon = reinterpret_cast<HICON>(::SendMessage(child->GetSafeHwnd(), BM_GETIMAGE, IMAGE_ICON, 0));
        if (icon)
        {
            ICONINFO info = {};
            int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
            if (::GetIconInfo(icon, &info))
            {
                BITMAP bm = {};
                if (info.hbmColor && ::GetObject(info.hbmColor, sizeof(bm), &bm))
                { cx = bm.bmWidth; cy = bm.bmHeight; }
                if (info.hbmColor) ::DeleteObject(info.hbmColor);
                if (info.hbmMask) ::DeleteObject(info.hbmMask);
            }
            const int x = rc.left + (rc.Width() - cx) / 2;
            const int y = rc.top + (rc.Height() - cy) / 2;
            // DrawState supports the disabled-icon effect; DrawIconEx has no
            // DI_GRAYED flag (and SDK declarations differ across toolsets).
            ::DrawState(dis->hDC, nullptr, nullptr,
                reinterpret_cast<LPARAM>(icon), 0,
                x, y, cx, cy,
                DST_ICON | (disabled ? DSS_DISABLED : DSS_NORMAL));
        }
        else
        {
            CString text;
            child->GetWindowText(text);
            dc->SetBkMode(TRANSPARENT);
            dc->SetTextColor(disabled ? c.disabledText : c.text);
            CFont* font = child->GetFont();
            CFont* oldFont = font ? dc->SelectObject(font) : nullptr;
            dc->DrawText(text, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (oldFont) dc->SelectObject(oldFont);
        }
    }

    if (state & ODS_FOCUS)
    {
        CRect focus(rc);
        focus.DeflateRect(3, 3);
        dc->DrawFocusRect(focus);
    }
}

void CThemeManager::RefreshWindow(HWND hwnd)
{
    if (hwnd && ::IsWindow(hwnd))
        ::RedrawWindow(hwnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

IMPLEMENT_DYNAMIC(CThemedSliderCtrl, CSliderCtrl)
BEGIN_MESSAGE_MAP(CThemedSliderCtrl, CSliderCtrl)
    ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CThemedSliderCtrl::OnCustomDraw)
END_MESSAGE_MAP()

void CThemedSliderCtrl::OnCustomDraw(NMHDR* header, LRESULT* result)
{
    if (!CThemeManager::IsDark())
    {
        *result = CDRF_DODEFAULT;
        return;
    }

    NMCUSTOMDRAW* draw = reinterpret_cast<NMCUSTOMDRAW*>(header);
    if (draw->dwDrawStage == CDDS_PREPAINT)
    {
        *result = CDRF_NOTIFYITEMDRAW;
        return;
    }

    if (draw->dwDrawStage != CDDS_ITEMPREPAINT)
    {
        *result = CDRF_DODEFAULT;
        return;
    }

    CDC* dc = CDC::FromHandle(draw->hdc);
    if (!dc)
    {
        *result = CDRF_DODEFAULT;
        return;
    }

    const CThemeManager::Colors& c = CThemeManager::GetColors();
    const bool enabled = IsWindowEnabled() != FALSE;
    CRect rc(draw->rc);

    if (draw->dwItemSpec == TBCD_CHANNEL)
    {
        CBrush track(c.control);
        CPen edge(PS_SOLID, 1, c.border);
        CBrush* oldBrush = dc->SelectObject(&track);
        CPen* oldPen = dc->SelectObject(&edge);
        CRect channel(rc);
        channel.DeflateRect(0, max(1, channel.Height() / 4));
        dc->RoundRect(channel, CPoint(channel.Height(), channel.Height()));

        // Preserve the progress/range selection used by the player timeline.
        if ((GetStyle() & TBS_ENABLESELRANGE) && GetRangeMax() > GetRangeMin())
        {
            CRect channelRect;
            GetChannelRect(&channelRect);
            CRect thumbRect;
            GetThumbRect(&thumbRect);
            const int lo = GetRangeMin(), hi = GetRangeMax();
            const int selLo = static_cast<int>(SendMessage(TBM_GETSELSTART));
            const int selHi = static_cast<int>(SendMessage(TBM_GETSELEND));
            if (channelRect.Width() > thumbRect.Width())
            {
                const int left = channelRect.left + thumbRect.Width() / 2;
                const int right = channelRect.right - thumbRect.Width() / 2;
                const int x1 = left + MulDiv(max(lo, min(hi, selLo)) - lo, right - left, hi - lo);
                const int x2 = left + MulDiv(max(lo, min(hi, selHi)) - lo, right - left, hi - lo);
                CRect selected(min(x1, x2), channel.top, max(x1, x2) + 1, channel.bottom);
                CBrush selection(c.selection);
                dc->FillRect(selected, &selection);
            }
        }
        dc->SelectObject(oldPen);
        dc->SelectObject(oldBrush);
        *result = CDRF_SKIPDEFAULT;
        return;
    }

    if (draw->dwItemSpec == TBCD_THUMB)
    {
        CBrush thumb(enabled ? c.buttonHot : c.control);
        CPen edge(PS_SOLID, 1, c.border);
        CBrush* oldBrush = dc->SelectObject(&thumb);
        CPen* oldPen = dc->SelectObject(&edge);
        rc.DeflateRect(1, 1);
        dc->RoundRect(rc, CPoint(max(3, rc.Width() / 3), max(3, rc.Height() / 3)));
        dc->SelectObject(oldPen);
        dc->SelectObject(oldBrush);
        *result = CDRF_SKIPDEFAULT;
        return;
    }

    *result = CDRF_DODEFAULT;
}

BEGIN_MESSAGE_MAP(CThemedDialog, CDialogEx)
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CThemedDialog::OnInitDialog()
{
    const BOOL result = __super::OnInitDialog();
    CThemeManager::RefreshWindow(GetSafeHwnd());
    return result;
}

HBRUSH CThemedDialog::OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor)
{
    return CThemeManager::OnCtlColor(dc, wnd, ctlColor);
}

BOOL CThemedDialog::OnEraseBkgnd(CDC* dc)
{
    CRect rc;
    GetClientRect(&rc);
    dc->FillSolidRect(&rc, CThemeManager::GetColors().window);
    return TRUE;
}
