
#include "stdafx.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "Player.h"
#endif

#include "PlayerDoc.h"
#include "PlayerViewD2D.h"

#include "I420Effect.h"

#include "decoderinterface.h"

#include <d2d1_2.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

enum { WM_DRAW_FRAME = WM_USER + 101 };

namespace {

//Use IDWriteTextLayout to get the text size
HRESULT GetTextSize(const WCHAR* text, IDWriteTextFormat* pTextFormat, const SIZE& sourceSize, D2D1_SIZE_F& size)
{
    HRESULT hr = S_OK;
    CComPtr<IDWriteTextLayout> pTextLayout;
    // Create a text layout
    auto len = wcslen(text);
    if (len > 0 && text[len - 1] == L'\n')
        --len;
    hr = AfxGetD2DState()->GetWriteFactory()->CreateTextLayout(
        text, 
        len,
        pTextFormat, 
        sourceSize.cx, 
        sourceSize.cy, 
        &pTextLayout);
    if (SUCCEEDED(hr))
    {
        //Gets the text size
        DWRITE_TEXT_METRICS textMetrics;
        hr = pTextLayout->GetMetrics(&textMetrics);
        size = D2D1::SizeF(textMetrics.width, textMetrics.height);
    }
    return hr;
}

} // namespace

class FrameListenerD2D : public IFrameListener
{
public:
    explicit FrameListenerD2D(CPlayerViewD2D* playerView) : m_playerView(playerView) {}

private:
    void updateFrame() override
    {
        m_playerView->updateFrame();
    }
    void drawFrame(IFrameDecoder*) override
    {
        m_playerView->SendNotifyMessage(WM_DRAW_FRAME, 0, 0);
    }

private:
    CPlayerViewD2D* m_playerView;
};


// CPlayerViewD2D

IMPLEMENT_DYNCREATE(CPlayerViewD2D, CView)

BEGIN_MESSAGE_MAP(CPlayerViewD2D, CView)
    // Standard printing commands
    ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CView::OnFilePrintPreview)
    ON_REGISTERED_MESSAGE(AFX_WM_DRAW2D, &CPlayerViewD2D::OnDraw2D)
    ON_MESSAGE(WM_DRAW_FRAME, &CPlayerViewD2D::DrawFrame)
    ON_WM_CREATE()
END_MESSAGE_MAP()

// CPlayerViewD2D construction/destruction

CPlayerViewD2D::CPlayerViewD2D()
    : m_frameListener(new FrameListenerD2D(this))
    , m_aspectRatio(1.f)
{
    // Enable D2D support for this window:
    EnableD2DSupport();
}

CPlayerViewD2D::~CPlayerViewD2D()
{
    GetDocument()->getFrameDecoder()->setFrameListener(NULL);
}

BOOL CPlayerViewD2D::PreCreateWindow(CREATESTRUCT& cs)
{
    // For the full screen mode
    cs.style &= ~WS_BORDER;
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;

    return CView::PreCreateWindow(cs);
}

// CPlayerViewD2D drawing

void CPlayerViewD2D::OnDraw(CDC* /*pDC*/)
{
    CPlayerDoc* pDoc = GetDocument();
    ASSERT_VALID(pDoc);
    if (!pDoc)
        return;

    // TODO: add draw code for native data here
}


// CPlayerViewD2D printing

BOOL CPlayerViewD2D::OnPreparePrinting(CPrintInfo* pInfo)
{
    // default preparation
    return DoPreparePrinting(pInfo);
}

void CPlayerViewD2D::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
    // TODO: add extra initialization before printing
}

void CPlayerViewD2D::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
    // TODO: add cleanup after printing
}


// CPlayerViewD2D diagnostics

#ifdef _DEBUG
void CPlayerViewD2D::AssertValid() const
{
    CView::AssertValid();
}

void CPlayerViewD2D::Dump(CDumpContext& dc) const
{
    CView::Dump(dc);
}
#endif //_DEBUG

CPlayerDoc* CPlayerViewD2D::GetDocument() const
{
    ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CPlayerDoc)));
    return static_cast<CPlayerDoc*>(m_pDocument);
}


// CPlayerViewD2D message handlers

afx_msg LRESULT CPlayerViewD2D::OnDraw2D(WPARAM, LPARAM lParam)
{
    CSingleLock lock(&m_csSurface, TRUE);

    CHwndRenderTarget* pRenderTarget = (CHwndRenderTarget*)lParam;
    ASSERT_VALID(pRenderTarget);

    CRect rect;
    GetClientRect(rect);

    if (rect.Width() <= 1 || rect.Height() <= 1)
        return TRUE;

    CComQIPtr<ID2D1DeviceContext> spContext(*pRenderTarget);

    float dpiX;
    float dpiY;
    spContext->GetDpi(&dpiX, &dpiY);

    float scaleW = rect.Width() / (float) m_sourceSize.cx;
    float scaleH = rect.Height() / (float) m_sourceSize.cy;

    D2D1_POINT_2F offset;
    if (scaleH * m_aspectRatio <= scaleW) {
        scaleW = scaleH * m_aspectRatio;
        offset.x = (rect.Width() -
            (m_sourceSize.cx * scaleW)) / 2.0f;
        offset.y = 0.0f;
    }
    else {
        scaleH = scaleW / m_aspectRatio;
        offset.x = 0.0f;
        offset.y = (rect.Height() -
            (m_sourceSize.cy * scaleH)) / 2.0f;
    }

    D2D1::Matrix3x2F transform =
        D2D1::Matrix3x2F::Scale(scaleW, scaleH) *
        D2D1::Matrix3x2F::Translation(offset.x, offset.y);

    spContext->SetTransform(transform);
    spContext->Clear(D2D1::ColorF(D2D1::ColorF::Black));
    if (m_spEffect)
    {
        spContext->DrawImage(m_spEffect,
            //D2D1_INTERPOLATION_MODE_CUBIC
            D2D1_INTERPOLATION_MODE_LINEAR
            );
    }

    auto subtitle = GetDocument()->getSubtitle();
    if (!subtitle.empty())
    {
        const auto& convertedSubtitle = CA2T(subtitle.c_str(), /*CP_UTF8*/ 1251);
        CComPtr<IDWriteTextFormat> pTextFormat;
        if (SUCCEEDED(AfxGetD2DState()->GetWriteFactory()->CreateTextFormat(
            L"MS Sans Serif",
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            max(m_sourceSize.cx / 50, 9),
            L"", //locale
            &pTextFormat)))
        {
            D2D1_SIZE_F boundingBox;
            if (SUCCEEDED(GetTextSize(convertedSubtitle, pTextFormat, m_sourceSize, boundingBox)))
            {
                const auto left = (m_sourceSize.cx - boundingBox.width) / 2;
                const auto top = m_sourceSize.cy - boundingBox.height - 2;

                CComPtr<ID2D1SolidColorBrush> pBlackBrush;
                CComPtr<ID2D1SolidColorBrush> pWhiteBrush;
                if (SUCCEEDED(spContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0f), &pBlackBrush)) &&
                    SUCCEEDED(spContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &pWhiteBrush)))
                {
                    spContext->DrawText(
                        convertedSubtitle,
                        wcslen(convertedSubtitle),
                        pTextFormat,
                        D2D1::RectF(left + 1, top + 1, left + 1 + boundingBox.width, top + 1 + boundingBox.height),
                        pBlackBrush);
                    spContext->DrawText(
                        convertedSubtitle,
                        wcslen(convertedSubtitle),
                        pTextFormat,
                        D2D1::RectF(left, top, left + boundingBox.width, top + boundingBox.height),
                        pWhiteBrush);
                }
            }
        }
    }

    return TRUE;
}


int CPlayerViewD2D::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CView::OnCreate(lpCreateStruct) == -1)
        return -1;

    GetDocument()->getFrameDecoder()->setFrameListener(m_frameListener.get());

    return 0;
}


void CPlayerViewD2D::updateFrame()
{
    FrameRenderingData data;
    if (!GetDocument()->getFrameDecoder()->getFrameRenderingData(&data))
    {
        return;
    }

    CSingleLock lock(&m_csSurface, TRUE);

    CHwndRenderTarget* renderTarget = LockRenderTarget();
    if (!renderTarget)
    {
        return;
    }

    m_aspectRatio = float(data.aspectNum) / data.aspectDen;

    CComQIPtr<ID2D1DeviceContext> spContext(*renderTarget);

    if (data.width != m_sourceSize.cx || data.height != m_sourceSize.cy)
    {
        m_sourceSize.cx = data.width;
        m_sourceSize.cy = data.height;

        if (!m_spEffect)
        {
            HRESULT hr = spContext->CreateEffect(CLSID_CustomI420Effect, &m_spEffect);
            if (FAILED(hr))
            {
                UnlockRenderTarget();
                return;
            }
        }
    }

    float dpiX;
    float dpiY;
    spContext->GetDpi(&dpiX, &dpiY);

    // Init bitmap properties in which will store the y (lumi) plane
    D2D1_BITMAP_PROPERTIES1 props;
    D2D1_PIXEL_FORMAT       pixFormat;

    pixFormat.alphaMode = D2D1_ALPHA_MODE_STRAIGHT;
    pixFormat.format = DXGI_FORMAT_A8_UNORM;
    props.pixelFormat = pixFormat;
    props.dpiX = dpiX;
    props.dpiY = dpiY;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
    props.colorContext = nullptr;

    CComPtr<ID2D1Bitmap1> yBitmap;
    HRESULT hr = spContext->CreateBitmap(
        { static_cast<UINT32>(m_sourceSize.cx), static_cast<UINT32>(m_sourceSize.cy) },
        data.image[0], data.pitch[0], props, &yBitmap);
    CComPtr<ID2D1Bitmap1> uBitmap;
    hr = spContext->CreateBitmap(
        { static_cast<UINT32>(m_sourceSize.cx / 2), static_cast<UINT32>(m_sourceSize.cy / 2) },
        data.image[1], data.pitch[1], props, &uBitmap);
    CComPtr<ID2D1Bitmap1> vBitmap;
    hr = spContext->CreateBitmap(
        { static_cast<UINT32>(m_sourceSize.cx / 2), static_cast<UINT32>(m_sourceSize.cy / 2) },
        data.image[2], data.pitch[2], props, &vBitmap);

    m_spEffect->SetInput(0, yBitmap);
    m_spEffect->SetInput(1, uBitmap);
    m_spEffect->SetInput(2, vBitmap);

    UnlockRenderTarget();
}

LRESULT CPlayerViewD2D::DrawFrame(WPARAM, LPARAM)
{
    CSingleLock lock(&m_csSurface, TRUE);

    CHwndRenderTarget* renderTarget = LockRenderTarget();
    if (renderTarget)
    {
        renderTarget->BeginDraw();
        OnDraw2D(0, (LPARAM)renderTarget);
        renderTarget->EndDraw();

        UnlockRenderTarget();
    }

    GetDocument()->getFrameDecoder()->finishedDisplayingFrame();

    return 0;
}
