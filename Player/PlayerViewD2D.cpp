
#include "stdafx.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "Player.h"
#endif

#include "PlayerDoc.h"
#include "PlayerViewD2D.h"

#include "I420Effect.h"

#include "GetClipboardText.h"

#include "decoderinterface.h"

#include <d2d1_2.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

enum { WM_DRAW_FRAME = WM_USER + 101 };

namespace {

//Use IDWriteTextLayout to get the text size
HRESULT GetTextSize(const std::wstring& text, IDWriteTextFormat* pTextFormat, const SIZE& sourceSize, D2D1_SIZE_F& size)
{
    CComPtr<IDWriteTextLayout> pTextLayout;
    // Create a text layout
    auto len = text.length();
    if (len > 0 && text[len - 1] == L'\n')
        --len;
    auto hr = AfxGetD2DState()->GetWriteFactory()->CreateTextLayout(
        text.c_str(),
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
    void updateFrame(IFrameDecoder* decoder, unsigned int generation) override
    {
        m_playerView->updateFrame();
        decoder->finishedDisplayingFrame(generation, IFrameDecoder::RELEASE_FRAME);
    }
    void drawFrame(IFrameDecoder*, unsigned int generation) override
    {
        m_playerView->SendNotifyMessage(WM_DRAW_FRAME, 0, generation);
    }
    void decoderClosing() override
    {
    }

private:
    CPlayerViewD2D* m_playerView;
};


// CPlayerViewD2D

IMPLEMENT_DYNCREATE(CPlayerViewD2D, CView)

BEGIN_MESSAGE_MAP(CPlayerViewD2D, CView)
    // Standard printing commands
    ON_COMMAND(ID_EDIT_PASTE, &CPlayerViewD2D::OnEditPaste)
    ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CView::OnFilePrintPreview)
    ON_REGISTERED_MESSAGE(AFX_WM_DRAW2D, &CPlayerViewD2D::OnDraw2D)
    ON_MESSAGE(WM_DRAW_FRAME, &CPlayerViewD2D::DrawFrame)
    ON_WM_CREATE()
    ON_WM_DROPFILES()
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
    GetDocument()->getFrameDecoder()->setFrameListener(nullptr);
}

BOOL CPlayerViewD2D::PreCreateWindow(CREATESTRUCT& cs)
{
    // For the full screen mode
    cs.style &= ~WS_BORDER;
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;

    return CView::PreCreateWindow(cs);
}

void CPlayerViewD2D::OnEditPaste()
{
    const auto text = GetClipboardText();
    if (!text.empty())
    {
        GetDocument()->OnEditPaste(text);
    }
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

    auto sourceSize = m_sourceSize;
    auto aspectRatio = m_aspectRatio;

    if (GetDocument()->isOrientationUpend())
    {
        std::swap(sourceSize.cx, sourceSize.cy);
        aspectRatio = 1. / aspectRatio;
    }

    float scaleW = rect.Width() / (float) sourceSize.cx;
    float scaleH = rect.Height() / (float) sourceSize.cy;

    D2D1_POINT_2F offset;
    if (scaleH * aspectRatio <= scaleW) {
        scaleW = scaleH * aspectRatio;
        offset.x = (rect.Width() -
            (sourceSize.cx * scaleW)) / 2.0f;
        offset.y = 0.0f;
    }
    else {
        scaleH = scaleW / aspectRatio;
        offset.x = 0.0f;
        offset.y = (rect.Height() -
            (sourceSize.cy * scaleH)) / 2.0f;
    }

    auto transform = D2D1::Matrix3x2F::Identity();
    if (GetDocument()->isOrientationUpend())
    {
        std::swap(transform._11, transform._21);
        std::swap(transform._12, transform._22);
    }
    if (GetDocument()->isOrientationMirrorx())
    {
        transform._11 = -transform._11;
        transform._21 = -transform._21;
        transform._31 += sourceSize.cx;
    }
    if (GetDocument()->isOrientationMirrory())
    {
        transform._12 = -transform._12;
        transform._22 = -transform._22;
        transform._32 += sourceSize.cy;
    }

    transform = transform *
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

    spContext->SetTransform(D2D1::Matrix3x2F::Scale(scaleW, scaleH) *
        D2D1::Matrix3x2F::Translation(offset.x, offset.y));

    if (auto subtitle = GetDocument()->getSubtitle(); !subtitle.empty())
    {
        CComPtr<IDWriteTextFormat> pTextFormat;
        if (SUCCEEDED(AfxGetD2DState()->GetWriteFactory()->CreateTextFormat(
            L"MS Sans Serif",
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            std::max<int>({ sourceSize.cx / 60, sourceSize.cy / 60, 9 }),
            L"", //locale
            &pTextFormat)))
        {
            D2D1_SIZE_F boundingBox;
            if (SUCCEEDED(GetTextSize(subtitle, pTextFormat, sourceSize, boundingBox)))
            {
                const auto left = (sourceSize.cx - boundingBox.width) / 2;
                const auto top = sourceSize.cy - boundingBox.height - 2;

                CComPtr<ID2D1SolidColorBrush> pBlackBrush;
                CComPtr<ID2D1SolidColorBrush> pWhiteBrush;
                if (SUCCEEDED(spContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0f), &pBlackBrush)) &&
                    SUCCEEDED(spContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &pWhiteBrush)))
                {
                    spContext->DrawText(
                        subtitle.c_str(),
                        subtitle.length(),
                        pTextFormat,
                        D2D1::RectF(left + 1, top + 1, left + 1 + boundingBox.width, top + 1 + boundingBox.height),
                        pBlackBrush);
                    spContext->DrawText(
                        subtitle.c_str(),
                        subtitle.length(),
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

    DragAcceptFiles();

    return 0;
}


void CPlayerViewD2D::updateFrame()
{
    FrameRenderingData data;
    if (!GetDocument()->getFrameDecoder()->getFrameRenderingData(&data))
    {
        return;
    }

    CHwndRenderTarget* renderTarget = LockRenderTarget();
    if (!renderTarget)
    {
        return;
    }

    m_aspectRatio = float(data.aspectNum) / data.aspectDen;

    m_sourceSize.cx = data.width;
    m_sourceSize.cy = data.height;

    CComQIPtr<ID2D1DeviceContext> spContext(*renderTarget);

    CSingleLock lock(&m_csSurface, TRUE);

    if (!m_spEffect)
    {
        HRESULT hr = spContext->CreateEffect(CLSID_CustomI420Effect, &m_spEffect);
        if (FAILED(hr))
        {
            UnlockRenderTarget();
            return;
        }
    }

    // Init bitmap properties in which will store the y (lumi) plane
    D2D1_BITMAP_PROPERTIES1 props;
    D2D1_PIXEL_FORMAT       pixFormat;

    pixFormat.alphaMode = D2D1_ALPHA_MODE_STRAIGHT;
    pixFormat.format = DXGI_FORMAT_A8_UNORM;
    props.pixelFormat = pixFormat;

    spContext->GetDpi(&props.dpiX, &props.dpiY);

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

LRESULT CPlayerViewD2D::DrawFrame(WPARAM, LPARAM generation)
{
    {
        CSingleLock lock(&m_csSurface, TRUE);
        DoD2DPaint();
    }

    GetDocument()->getFrameDecoder()->finishedDisplayingFrame(generation, IFrameDecoder::FINALIZE_DISPLAY);

    return 0;
}


BOOL CPlayerViewD2D::OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
    if (message == WM_PAINT)
    {
        CSingleLock lock(&m_csSurface, TRUE);
        const BOOL lResult = DoD2DPaint();
        if (pResult != NULL)
            *pResult = lResult;
        return lResult;
    }

    return __super::OnWndMsg(message, wParam, lParam, pResult);
}


void CPlayerViewD2D::OnDropFiles(HDROP hDropInfo)
{
    GetDocument()->OnDropFiles(hDropInfo);
    CView::OnDropFiles(hDropInfo);
}
