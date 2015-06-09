
// PlayerView.h : interface of the CPlayerView class
//

#pragma once

#include <memory>

struct IFrameListener;

struct ID2D1Effect;

class CPlayerView : public CView
{
friend class FrameListener;

protected: // create from serialization only
    CPlayerView();
    DECLARE_DYNCREATE(CPlayerView)

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

// Implementation
public:
    virtual ~CPlayerView();
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
public:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
protected:
    void updateFrame();

private:
    std::unique_ptr<IFrameListener> m_frameListener;
    CSize m_sourceSize;
    CComPtr<ID2D1Effect> m_spEffect;
};

#ifndef _DEBUG  // debug version in PlayerView.cpp
inline CPlayerDoc* CPlayerView::GetDocument() const
   { return reinterpret_cast<CPlayerDoc*>(m_pDocument); }
#endif

