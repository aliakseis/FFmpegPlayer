#pragma once

#include "IEraseableArea.h"

#include <memory>

struct IFrameListener;

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct IDirectXVideoProcessorService;
struct IDirectXVideoProcessor;

class CD3DFont;

class CPlayerDoc;

#define USE_DXVA2

// CPlayerView view

class CPlayerView : public CView, public IEraseableArea
{
    friend class FrameListenerDxva2;

    DECLARE_DYNCREATE(CPlayerView)

protected:
    CPlayerView();           // protected constructor used by dynamic creation
    virtual ~CPlayerView();

public:
    virtual void OnDraw(CDC* pDC);      // overridden to draw this view
#ifdef _DEBUG
    virtual void AssertValid() const;
#ifndef _WIN32_WCE
    virtual void Dump(CDumpContext& dc) const;
#endif
#endif

    CPlayerDoc* GetDocument() const;

    void updateFrame();

    void OnErase(CWnd* pInitiator, CDC* pDC, BOOL isFullScreen) override;

protected:
    DECLARE_MESSAGE_MAP()
private:
    bool InitializeD3D9();
    bool InitializeExtra(bool createSurface);
    void DestroyExtra();
    void DestroyD3D9();
#ifdef USE_DXVA2
    bool CreateDXVA2VPDevice(REFGUID guid, bool bDXVA2SW, bool createSurface);
#endif
    bool ResetDevice();
    bool ProcessVideo();

    CRect GetTargetRect();

private:
    std::unique_ptr<IFrameListener> m_frameListener;
    CSize m_sourceSize;
    CSize m_aspectRatio;

    CCriticalSection m_csSurface;

    CComPtr<IDirect3D9> m_pD3D9;
    CComPtr<IDirect3DDevice9>  m_pD3DD9;
    CComPtr<IDirect3DSurface9> m_pD3DRT;
    CComPtr<IDirect3DSurface9> m_pMainStream;

#ifdef USE_DXVA2
    CComPtr<IDirectXVideoProcessorService> m_pDXVAVPS;
    CComPtr<IDirectXVideoProcessor> m_pDXVAVPD;

    LONG m_ProcAmpValues[4] {};
    LONG m_NFilterValues[6] {};
    LONG m_DFilterValues[6] {};
#endif

    std::unique_ptr<CD3DFont> m_subtitleFont;

public:
    afx_msg void OnPaint();
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    virtual void OnUpdate(CView* /*pSender*/, LPARAM /*lHint*/, CObject* /*pHint*/);
};
