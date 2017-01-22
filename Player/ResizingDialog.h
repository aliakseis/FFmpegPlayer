#pragma once

// ResizingDialog.h : header file
//

#include <map>
#include <utility>

namespace ResizingDialogHelpers
{
struct CoordinateStrategy
{
    // This is a strategy of resizing of one control coordinate (left, right, top or bottom).
    //
    // TYPE_CONST means that Argument specifies the unchangeable coordinate value
    // TYPE_FACTOR means that coordinate will change from its start size proportionally
    //				to difference between window current and start sizes.
    // TYPE_PERCENT means that coordinate will always be calculated as (WindowSize*Argument/100)

    enum STRATEGY_TYPE
    {
        TYPE_CONST,
        TYPE_FACTOR,
        TYPE_PERCENT
    };

    CoordinateStrategy(STRATEGY_TYPE Type = TYPE_FACTOR, double Arg = 0.0)
    {
        m_type = Type;
        m_param = Arg;
    }

    bool operator==(const CoordinateStrategy& b) const
    {
        return m_type == b.m_type && m_param == b.m_param;
    }

    inline void Process(long& lResultCoordinate, long lCurrentClientWndDelta, long lClientWndSize,
                        long lFirstControlSize, long lFirstClientWndSize) const;

   protected:
    STRATEGY_TYPE m_type;
    double m_param;
};

/////////////////////////////////////////

inline CoordinateStrategy Coord() { return CoordinateStrategy(); }

inline CoordinateStrategy CoordPercent(double percent)
{
    return CoordinateStrategy(CoordinateStrategy::TYPE_PERCENT, percent);
}

inline CoordinateStrategy CoordFactor(double factor)
{
    return CoordinateStrategy(CoordinateStrategy::TYPE_FACTOR, factor);
}

inline CoordinateStrategy CoordConst(LONG value)
{
    return CoordinateStrategy(CoordinateStrategy::TYPE_CONST, value);
}

};  // namespace ResizingDialogHelpers

/////////////////////////////////////////////////////////////////////////////
// ResizingDialogStrategy dialog

class ResizingDialogStrategy
{
    // Construction
   public:
    ResizingDialogStrategy(HWND hWnd = NULL);

    void SetDialog(HWND hWnd) { m_hWnd = hWnd; }

    //////////////////////////////////////////////////////////////////////////
    typedef const ResizingDialogHelpers::CoordinateStrategy& COORD;

    bool AddChildInfo(UINT nID, COORD left, COORD top, COORD right, COORD bottom);
    bool AddChildInfo(HWND hWnd, COORD left, COORD top, COORD right, COORD bottom);

    void RefreshChildSizes();

    void OnInitDialog();
    void OnSize(UINT nType, int cx, int cy);

    // Implementation
   private:
    static BOOL CALLBACK InitializeEnumChildProc(HWND hwnd, LPARAM lParam);
    static BOOL CALLBACK UpdateEnumChildProc(HWND hwnd, LPARAM lParam);

    HWND m_hWnd;

    bool m_bInitialized;
    SIZE m_FirstClientSize;
    SIZE m_OldClientSize;

    struct ITEM_INFO
    {
        BOOL bInitializedManually;
        RECT rect;
        ResizingDialogHelpers::CoordinateStrategy left, right, top, bottom;
    };

    typedef std::map<HWND, ITEM_INFO> ChildMap;
    ChildMap m_children;
};

/////////////////////////////////////////////////////////////////////////////
// MinMaxDialogStrategy dialog

class MinMaxDialogStrategy
{
   public:
    MinMaxDialogStrategy();
    void OnGetMinMaxInfo(MINMAXINFO* pMinMax);

    void SetWindowMinSize(SIZE sz);
    void SetWindowMaxSize(SIZE sz);

   protected:
    SIZE m_minSize;
    SIZE m_maxSize;
};

/////////////////////////////////////////////////////////////////////////////
// CResizeDialog dialog

template <typename T>
class CResizeDialog : public T
{
   public:
    // using T::T; does not work
    template <typename... Args>
    CResizeDialog(Args&&... args)
        : T(std::forward<Args>(args)...)
    {
    }

    ResizingDialogStrategy m_strategyResizing;
    MinMaxDialogStrategy m_strategyMinMax;

    // Overrides
   protected:
    // We can't use message maps in this class
    BOOL OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) override
    {
        switch (message)
        {
        case WM_SIZE:
            m_strategyResizing.OnSize(wParam, UINT32(LOWORD(lParam)), UINT32(HIWORD(lParam)));
            break;
        case WM_GETMINMAXINFO:
            OnGetMinMaxInfo((MINMAXINFO*)lParam);
            break;
        }
        const BOOL result = T::OnWndMsg(message, wParam, lParam, pResult);
        __if_not_exists(T::OnInitDialog)
        {
            if (WM_INITDIALOG == message)
            {
                OnInitDialog();
            }
        }
        return result;
    }

    // Implementation
   protected:
    virtual BOOL OnInitDialog()
    {
        BOOL result = TRUE;
        __if_exists(T::OnInitDialog) { result = T::OnInitDialog(); }
        __if_not_exists(T::OnInitDialog)
        {
            if (!UpdateData(FALSE))
            {
                TRACE0("Warning: UpdateData failed during dialog init.\n");
                result = FALSE;
            }
        }

        m_strategyResizing.SetDialog(this->GetSafeHwnd());
        m_strategyResizing.OnInitDialog();

        return result;  // return TRUE unless you set the focus to a control
    }

    afx_msg void OnGetMinMaxInfo(MINMAXINFO* pMinMax)
    {
        T::OnGetMinMaxInfo(pMinMax);
        m_strategyMinMax.OnGetMinMaxInfo(pMinMax);
    }
};
