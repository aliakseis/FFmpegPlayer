#pragma once

#include <d2d1effectauthor.h>

// {3AB41678-D4BC-4BE1-8A91-07A63DEEFEA1}
DEFINE_GUID(GUID_I420PixelShader, 0x3ab41678, 0xd4bc, 0x4be1, 0x8a, 0x91, 0x7, 0xa6, 0x3d, 0xee, 0xfe, 0xa1);
// {DA637E40-44D4-4617-A3D3-A28344549EEA}
DEFINE_GUID(CLSID_CustomI420Effect, 0xda637e40, 0x44d4, 0x4617, 0xa3, 0xd3, 0xa2, 0x83, 0x44, 0x54, 0x9e, 0xea);

class I420Effect : public ID2D1EffectImpl, public ID2D1DrawTransform
{
public:
    // Declare effect registration methods.
    static HRESULT Register(_In_ ID2D1Factory1* pFactory);
    static HRESULT __stdcall CreateRippleImpl(_Outptr_ IUnknown** ppEffectImpl);

    // Declare ID2D1EffectImpl implementation methods.
    IFACEMETHODIMP Initialize(_In_ ID2D1EffectContext* pContextInternal, _In_ ID2D1TransformGraph* pTransformGraph);
    IFACEMETHODIMP PrepareForRender(D2D1_CHANGE_TYPE changeType);
    IFACEMETHODIMP SetGraph(_In_ ID2D1TransformGraph* pGraph);

    // Declare ID2D1DrawTransform implementation methods.
    IFACEMETHODIMP SetDrawInfo(_In_ ID2D1DrawInfo* pRenderInfo);

    // Declare ID2D1Transform implementation methods.
    IFACEMETHODIMP MapOutputRectToInputRects(
        _In_ const D2D1_RECT_L* pOutputRect,
        _Out_writes_(inputRectCount) D2D1_RECT_L* pInputRects,
        UINT32 inputRectCount
        ) const;

    IFACEMETHODIMP MapInputRectsToOutputRect(
        _In_reads_(inputRectCount) CONST D2D1_RECT_L* pInputRects,
        _In_reads_(inputRectCount) CONST D2D1_RECT_L* pInputOpaqueSubRects,
        UINT32 inputRectCount,
        _Out_ D2D1_RECT_L* pOutputRect,
        _Out_ D2D1_RECT_L* pOutputOpaqueSubRect
        );

    IFACEMETHODIMP MapInvalidRect(
        UINT32 inputIndex,
        D2D1_RECT_L invalidInputRect,
        _Out_ D2D1_RECT_L* pInvalidOutputRect
        ) const;

    // Declare ID2D1TransformNode implementation methods.
    IFACEMETHODIMP_(UINT32) GetInputCount() const;

    // Declare IUnknown implementation methods.
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();
    IFACEMETHODIMP QueryInterface(_In_ REFIID riid, _Outptr_ void** ppOutput);
private:

    I420Effect();

    CComPtr<ID2D1DrawInfo>      m_drawInfo;

    LONG                        m_refCount;
    D2D1_RECT_L                 m_inputRect;
    D2D1_SIZE_U                 m_originalFrame;
};
