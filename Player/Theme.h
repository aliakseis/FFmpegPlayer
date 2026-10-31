#pragma once

#include <afxwin.h>
#include <afxdialogex.h>
#include <afxcmn.h>
#include <afxvisualmanageroffice2007.h>

// Centralized palette and drawing helpers for the application.
class CThemeManager
{
public:
    struct Colors
    {
        COLORREF window;
        COLORREF text;
        COLORREF control;
        COLORREF controlText;
        COLORREF disabledText;
        COLORREF border;
        COLORREF selection;
        COLORREF selectionText;
        COLORREF buttonHot;
        COLORREF buttonPressed;
    };

    static void SetDark(bool dark);
    static bool IsDark();
    static const Colors& GetColors();
    static HBRUSH GetWindowBrush();
    static HBRUSH GetControlBrush();
    static HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor);
    static void DrawButton(CWnd* parent, LPDRAWITEMSTRUCT dis, bool checkBox = false, bool checked = false, HICON icon = nullptr);
    static void RefreshWindow(HWND hwnd);

private:
    static void EnsureBrushes();
};


// Application-specific Office 2007 visual manager. The built-in Obsidian
// scheme supplies the general MFC look; these overrides make popup menus and
// pane backgrounds use the same palette as the dialog controls.
class CPlayerVisualManager : public CMFCVisualManagerOffice2007
{
    DECLARE_DYNCREATE(CPlayerVisualManager)
public:
    void OnFillBarBackground(CDC* dc, CBasePane* bar, CRect client,
        CRect clip, BOOL nonClient = FALSE) override;
    void OnFillPopupWindowBackground(CDC* dc, CRect rect) override;
    void OnDrawMenuBorder(CDC* dc, CMFCPopupMenu* menu, CRect rect) override;
    void OnHighlightMenuItem(CDC* dc, CMFCToolBarMenuButton* button,
        CRect rect, COLORREF& textColor) override;
    COLORREF GetMenuItemTextColor(CMFCToolBarMenuButton* button,
        BOOL highlighted, BOOL disabled) override;
    COLORREF GetHighlightedMenuItemTextColor(
        CMFCToolBarMenuButton* button) override;
    COLORREF OnDrawMenuLabel(CDC* dc, CRect rect) override;
};

// Custom-drawn trackbar. It retains the native trackbar's input, range,
// selection and accessibility behavior; only its visual representation changes.
class CThemedSliderCtrl : public CSliderCtrl
{
    DECLARE_DYNAMIC(CThemedSliderCtrl)

protected:
    afx_msg void OnCustomDraw(NMHDR* header, LRESULT* result);
    DECLARE_MESSAGE_MAP()
};

// Use this as the base class for ordinary application dialogs.
class CThemedDialog : public CDialogEx
{
    DECLARE_DYNAMIC(CThemedDialog)
public:
    explicit CThemedDialog(UINT templateId, CWnd* parent = nullptr)
        : CDialogEx(templateId, parent) {}

protected:
    BOOL OnInitDialog() override;
    HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor);
    BOOL OnEraseBkgnd(CDC* dc);

    DECLARE_MESSAGE_MAP()
};
