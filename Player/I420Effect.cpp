#include "stdafx.h"
#include <initguid.h>

#include "I420Effect.h"

#include "I420Effect_PS.h"


I420Effect::I420Effect() :
m_refCount(1)
{
    m_originalFrame = D2D1::SizeU(0, 0);
}

HRESULT I420Effect::Register(_In_ ID2D1Factory1* pFactory)
{
    // Format Effect metadata in XML as expected
    PCWSTR pszXml =
        LR"(<?xml version='1.0' ?>
        <Effect>
        <!--System Properties-->
        <Property name='DisplayName' type='string' value='I420' />
        <Property name='Author' type='string' value='Public' />
        <Property name='Category' type='string' value='CODEC' />
        <Property name='Description' type='string' value='Adds a I420 Support' />
        <Inputs>
            <Input name='ySource' />
            <Input name='uSource' />
            <Input name='vSource' />
        </Inputs>
        </Effect>)";

    // Register the effect in the factory
    return pFactory->RegisterEffectFromString(
        CLSID_CustomI420Effect,
        pszXml,
        nullptr,
        0,
        CreateRippleImpl
        );
}

HRESULT __stdcall I420Effect::CreateRippleImpl(_Outptr_ IUnknown** ppEffectImpl)
{
    // Since the object's refcount is initialized to 1, we don't need to AddRef here.
    *ppEffectImpl = static_cast<ID2D1EffectImpl*>(new (std::nothrow) I420Effect());

    if (*ppEffectImpl == nullptr)
        return E_OUTOFMEMORY;
    return S_OK;
}


IFACEMETHODIMP I420Effect::Initialize(
    _In_ ID2D1EffectContext* pEffectContext,
    _In_ ID2D1TransformGraph* pTransformGraph
    )
{
    HRESULT hr = pEffectContext->LoadPixelShader(GUID_I420PixelShader, I420Effect_ByteCode, ARRAYSIZE(I420Effect_ByteCode));
    if (SUCCEEDED(hr))
    {
        // This loads the shader into the Direct2D image effects system and associates it with the GUID passed in.
        // If this method is called more than once (say by other instances of the effect) with the same GUID,
        // the system will simply do nothing, ensuring that only one instance of a shader is stored regardless of how
        // many time it is used.

        // The graph consists of a single transform. In fact, this class is the transform,
        // reducing the complexity of implementing an effect when all we need to
        // do is use a single pixel shader.
        hr = pTransformGraph->SetSingleTransformNode(this);
    }

    return S_OK;
}

IFACEMETHODIMP I420Effect::PrepareForRender(D2D1_CHANGE_TYPE /*changeType*/)
{
    return S_OK;
}

// SetGraph is only called when the number of inputs changes. This never happens as we publish this effect
// as a single input effect.
IFACEMETHODIMP I420Effect::SetGraph(_In_ ID2D1TransformGraph* /*pGraph*/)
{
    return E_NOTIMPL;
}

// Called to assign a new render info class, which is used to inform D2D on
// how to set the state of the GPU.
IFACEMETHODIMP I420Effect::SetDrawInfo(_In_ ID2D1DrawInfo* pDrawInfo)
{
    HRESULT hr = S_OK;

    m_drawInfo = pDrawInfo;

    hr = m_drawInfo->SetPixelShader(GUID_I420PixelShader);

    if (SUCCEEDED(hr))
    {
        // Providing this hint allows D2D to optimize performance when processing large images.
        m_drawInfo->SetInstructionCountHint(sizeof(I420Effect_ByteCode));
    }

    return hr;
}

// Calculates the mapping between the output and input rects. In this case,
// we want to request an expanded region to account for pixels that the ripple
// may need outside of the bounds of the destination.
IFACEMETHODIMP I420Effect::MapOutputRectToInputRects(
    _In_ const D2D1_RECT_L* /*pOutputRect*/,
    _Out_writes_(inputRectCount) D2D1_RECT_L* pInputRects,
    UINT32 inputRectCount
    ) const
{
    if (inputRectCount != 3)
        return E_NOTIMPL;

    pInputRects[0] = m_inputRect;
    pInputRects[1] = pInputRects[2] = { m_inputRect.left / 2, m_inputRect.top / 2, m_inputRect.right / 2, m_inputRect.bottom / 2 };

    return S_OK;
}

IFACEMETHODIMP I420Effect::MapInputRectsToOutputRect(
    _In_reads_(inputRectCount) CONST D2D1_RECT_L* pInputRects,
    _In_reads_(inputRectCount) CONST D2D1_RECT_L* /*pInputOpaqueSubRects*/,
    UINT32 /*inputRectCount*/,
    _Out_ D2D1_RECT_L* pOutputRect,
    _Out_ D2D1_RECT_L* pOutputOpaqueSubRect
    )
{
    *pOutputRect = pInputRects[0];

    if (m_inputRect.bottom != pInputRects[0].bottom
        || m_inputRect.top != pInputRects[0].top
        || m_inputRect.right != pInputRects[0].right
        || m_inputRect.left != pInputRects[0].left)
    {
        m_inputRect = pInputRects[0];
        m_originalFrame.width = m_inputRect.right;
        m_originalFrame.height = m_inputRect.bottom;
    }

    // Indicate that entire output might contain transparency.
    ZeroMemory(pOutputOpaqueSubRect, sizeof(*pOutputOpaqueSubRect));

    return S_OK;
}

IFACEMETHODIMP I420Effect::MapInvalidRect(
    UINT32 /*inputIndex*/,
    D2D1_RECT_L /*invalidInputRect*/,
    _Out_ D2D1_RECT_L* pInvalidOutputRect
    ) const
{
    HRESULT hr = S_OK;

    // Indicate that the entire output may be invalid.
    *pInvalidOutputRect = m_inputRect;

    return hr;
}

IFACEMETHODIMP_(UINT32) I420Effect::GetInputCount() const
{
    return 3;
}

// D2D ensures that that effects are only referenced from one thread at a time.
// To improve performance, we simply increment/decrement our reference count
// rather than use atomic InterlockedIncrement()/InterlockedDecrement() functions.
IFACEMETHODIMP_(ULONG) I420Effect::AddRef()
{
    ++m_refCount;
    return m_refCount;
}

IFACEMETHODIMP_(ULONG) I420Effect::Release()
{
    --m_refCount;

    if (m_refCount == 0)
    {
        delete this;
        return 0;
    }
    else
    {
        return m_refCount;
    }
}

// This enables the stack of parent interfaces to be queried. In the instance
// of the Ripple interface, this method simply enables the developer
// to cast a Ripple instance to an ID2D1EffectImpl or IUnknown instance.
IFACEMETHODIMP I420Effect::QueryInterface(_In_ REFIID riid, _Outptr_ void** ppOutput)
{
    static const QITAB rgqit[] =
    {
        QITABENT(I420Effect, ID2D1EffectImpl),
        QITABENT(I420Effect, ID2D1DrawTransform),
        QITABENT(I420Effect, ID2D1Transform),
        QITABENT(I420Effect, ID2D1TransformNode),
        { 0 },
    };

    return QISearch(this, rgqit, riid, ppOutput);
}
