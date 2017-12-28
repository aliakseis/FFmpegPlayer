
#pragma once

#include <memory>

struct IFrameListener;

struct ID2D1Effect;

class CPlayerViewD2D : public CView
{
friend class FrameListenerD2D;

protected: // create from serialization only
    CPlayerViewD2D();
    DECLARE_DYNCREATE(CPlayerViewD2D)

// Attributes
public:
    CPlayerDoc* GetDocument() const;

// Operations
public:

// Overrides
public:
    virtual void OnDraw(CDC* pDC);  // overridden to draw this view
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
    virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
    virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
    virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

    BOOL OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) override;

// Implementation
public:
    virtual ~CPlayerViewD2D();
#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif

protected:

private:

// Generated message map functions
protected:
    DECLARE_MESSAGE_MAP()
    afx_msg LRESULT OnDraw2D(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT DrawFrame(WPARAM wParam, LPARAM lParam);
public:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
protected:
    void updateFrame();

private:
    std::unique_ptr<IFrameListener> m_frameListener;
    CSize m_sourceSize;
    float m_aspectRatio;
    CComPtr<ID2D1Effect> m_spEffect;

    CCriticalSection m_csSurface;
};

