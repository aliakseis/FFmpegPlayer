// PlayerViewDxva2.cpp : implementation file
//

#include "stdafx.h"
#include "Player.h"
#include "PlayerView.h"
#include "PlayerDoc.h"

#include "decoderinterface.h"

#include "D3DFont.h"

#include <initguid.h>
#include <d3d9.h>

#ifdef USE_DXVA2
#include <dxva2api.h>
#endif


#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define CONVERT_FROM_YUV420P

namespace {

#ifdef USE_DXVA2
const UINT VIDEO_REQUIED_OP = DXVA2_VideoProcess_YUV2RGB |
                              DXVA2_VideoProcess_StretchX |
                              DXVA2_VideoProcess_StretchY/* |
                              DXVA2_VideoProcess_SubRects |
                              DXVA2_VideoProcess_SubStreams*/;
#endif

const D3DFORMAT VIDEO_RENDER_TARGET_FORMAT = D3DFMT_X8R8G8B8;
const D3DFORMAT VIDEO_MAIN_FORMAT = D3DFMT_YUY2;
//const D3DFORMAT VIDEO_MAIN_FORMAT = (D3DFORMAT)MAKEFOURCC('I', 'M', 'C', '3');

const UINT BACK_BUFFER_COUNT = 1;
const UINT SUB_STREAM_COUNT = 0;
const UINT DWM_BUFFER_COUNT = 4;

const UINT VIDEO_FPS = 60;


HMODULE g_hRgb9rastDLL = NULL;

PVOID g_pfnD3D9GetSWInfo = NULL;

//////////////////////////////////////////////////////////////////////////////

DWORD RGBtoYUV(const D3DCOLOR rgb)
{
    const INT A = HIBYTE(HIWORD(rgb));
    const INT R = LOBYTE(HIWORD(rgb)) - 16;
    const INT G = HIBYTE(LOWORD(rgb)) - 16;
    const INT B = LOBYTE(LOWORD(rgb)) - 16;

    //
    // studio RGB [16...235] to SDTV ITU-R BT.601 YCbCr
    //
    INT Y = (77 * R + 150 * G + 29 * B + 128) / 256 + 16;
    INT U = (-44 * R - 87 * G + 131 * B + 128) / 256 + 128;
    INT V = (131 * R - 110 * G - 21 * B + 128) / 256 + 128;

    return D3DCOLOR_AYUV(A, Y, U, V);
}

BOOL RegisterSoftwareRasterizer(IDirect3D9* g_pD3D9)
{
    if (!g_hRgb9rastDLL)
    {
        return FALSE;
    }

    HRESULT hr = g_pD3D9->RegisterSoftwareDevice(g_pfnD3D9GetSWInfo);

    if (FAILED(hr))
    {
        TRACE("RegisterSoftwareDevice failed with error 0x%x.\n", hr);
        return FALSE;
    }

    return TRUE;
}

BOOL InitializeModule()
{
    //
    // Load these DLLs dynamically because these may not be available prior to Vista.
    //
    g_hRgb9rastDLL = LoadLibrary(TEXT("rgb9rast.dll"));

    if (!g_hRgb9rastDLL)
    {
        TRACE("LoadLibrary(rgb9rast.dll) failed with error %d.\n", GetLastError());
    }
    else
    {
        g_pfnD3D9GetSWInfo = GetProcAddress(g_hRgb9rastDLL, "D3D9GetSWInfo");

        if (!g_pfnD3D9GetSWInfo)
        {
            TRACE("GetProcAddress(D3D9GetSWInfo) failed with error %d.\n", GetLastError());
            return FALSE;
        }
    }

    return TRUE;
}

D3DPRESENT_PARAMETERS GetD3dPresentParams(HWND hWnd)
{
    D3DPRESENT_PARAMETERS D3DPP = { 0 };

    D3DPP.BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
    D3DPP.BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);

    D3DPP.BackBufferFormat = VIDEO_RENDER_TARGET_FORMAT;
    D3DPP.BackBufferCount = BACK_BUFFER_COUNT;
    D3DPP.SwapEffect = D3DSWAPEFFECT_DISCARD;
    D3DPP.hDeviceWindow = hWnd;
    D3DPP.Windowed = TRUE;//g_bWindowed;
    D3DPP.Flags = D3DPRESENTFLAG_VIDEO;
    D3DPP.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    D3DPP.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    return D3DPP;
}

#ifdef USE_DXVA2

DXVA2_VideoDesc GetVideoDesc(const CSize& sourceSize)
{
    DXVA2_VideoDesc videoDesc;

    videoDesc.SampleWidth = sourceSize.cx;
    videoDesc.SampleHeight = sourceSize.cy;
    videoDesc.SampleFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
    videoDesc.SampleFormat.NominalRange = DXVA2_NominalRange_16_235;
    videoDesc.SampleFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT601;
    videoDesc.SampleFormat.VideoLighting = DXVA2_VideoLighting_dim;
    videoDesc.SampleFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
    videoDesc.SampleFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;
    videoDesc.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
    videoDesc.Format = VIDEO_MAIN_FORMAT;
    videoDesc.InputSampleFreq.Numerator = VIDEO_FPS;
    videoDesc.InputSampleFreq.Denominator = 1;
    videoDesc.OutputFrameFreq.Numerator = VIDEO_FPS;
    videoDesc.OutputFrameFreq.Denominator = 1;

    return videoDesc;
}

DXVA2_AYUVSample16 GetBackgroundColor()
{
    const D3DCOLOR yuv = RGBtoYUV(0);

    const BYTE Y = LOBYTE(HIWORD(yuv));
    const BYTE U = HIBYTE(LOWORD(yuv));
    const BYTE V = LOBYTE(LOWORD(yuv));

    DXVA2_AYUVSample16 color;

    color.Cr = V * 0x100;
    color.Cb = U * 0x100;
    color.Y = Y * 0x100;
    color.Alpha = 0xFFFF;

    return color;
}

const LONGLONG start_100ns = 0;// frame * LONGLONG(VIDEO_100NSPF);
const LONGLONG end_100ns = 0;// start_100ns + LONGLONG(VIDEO_100NSPF);

DXVA2_VideoProcessBltParams GetVideoProcessBltParams(
    const CRect& target,
    const LONG (&m_ProcAmpValues)[4],
    const LONG (&m_NFilterValues)[6],
    const LONG (&m_DFilterValues)[6])
{
    DXVA2_VideoProcessBltParams blt {};

    // Initialize VPBlt parameters.
    blt.TargetFrame = start_100ns;
    blt.TargetRect = target;

    // DXVA2_VideoProcess_Constriction
    blt.ConstrictionSize.cx = target.Width();
    blt.ConstrictionSize.cy = target.Height();

    blt.BackgroundColor = GetBackgroundColor();

    // DXVA2_VideoProcess_YUV2RGBExtended
    blt.DestFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_Unknown;
    blt.DestFormat.NominalRange = DXVA2_NominalRange_Unknown;
    blt.DestFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_Unknown;
    blt.DestFormat.VideoLighting = DXVA2_VideoLighting_dim;
    blt.DestFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
    blt.DestFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;

    blt.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame;

    // DXVA2_ProcAmp_Brightness
    blt.ProcAmpValues.Brightness.ll = m_ProcAmpValues[0];
    // DXVA2_ProcAmp_Contrast
    blt.ProcAmpValues.Contrast.ll = m_ProcAmpValues[1];
    // DXVA2_ProcAmp_Hue
    blt.ProcAmpValues.Hue.ll = m_ProcAmpValues[2];
    // DXVA2_ProcAmp_Saturation
    blt.ProcAmpValues.Saturation.ll = m_ProcAmpValues[3];

    // DXVA2_VideoProcess_AlphaBlend
    blt.Alpha = DXVA2_Fixed32OpaqueAlpha();

    // DXVA2_VideoProcess_NoiseFilter
    blt.NoiseFilterLuma.Level.ll = m_NFilterValues[0];
    blt.NoiseFilterLuma.Threshold.ll = m_NFilterValues[1];
    blt.NoiseFilterLuma.Radius.ll = m_NFilterValues[2];
    blt.NoiseFilterChroma.Level.ll = m_NFilterValues[3];
    blt.NoiseFilterChroma.Threshold.ll = m_NFilterValues[4];
    blt.NoiseFilterChroma.Radius.ll = m_NFilterValues[5];

    // DXVA2_VideoProcess_DetailFilter
    blt.DetailFilterLuma.Level.ll = m_DFilterValues[0];
    blt.DetailFilterLuma.Threshold.ll = m_DFilterValues[1];
    blt.DetailFilterLuma.Radius.ll = m_DFilterValues[2];
    blt.DetailFilterChroma.Level.ll = m_DFilterValues[3];
    blt.DetailFilterChroma.Threshold.ll = m_DFilterValues[4];
    blt.DetailFilterChroma.Radius.ll = m_DFilterValues[5];

    return blt;
}

DXVA2_VideoSample GetVideoSample(
    const CSize& m_sourceSize,
    const CRect& target,
    IDirect3DSurface9* srcSurface)
{
    DXVA2_VideoSample sample {};

    // Initialize main stream video sample.
    sample.Start = start_100ns;
    sample.End = end_100ns;

    // DXVA2_VideoProcess_YUV2RGBExtended
    sample.SampleFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
    sample.SampleFormat.NominalRange = DXVA2_NominalRange_16_235;
    sample.SampleFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT601;
    sample.SampleFormat.VideoLighting = DXVA2_VideoLighting_dim;
    sample.SampleFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
    sample.SampleFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;

    sample.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;

    sample.SrcSurface = srcSurface; //m_pMainStream;

    // DXVA2_VideoProcess_SubRects
    sample.SrcRect = { 0, 0, m_sourceSize.cx, m_sourceSize.cy };

    // DXVA2_VideoProcess_StretchX, Y
    sample.DstRect = target;

    // DXVA2_VideoProcess_PlanarAlpha
    sample.PlanarAlpha = DXVA2FloatToFixed(1.f);

    return sample;
}

#endif

void SimdCopyAndConvert(
    __m128i* const __restrict origin0,
    __m128i* const __restrict origin1,
    const __m128i* const __restrict src00,
    const __m128i* const __restrict src01,
    const __m64* const __restrict src0,
    const __m64* const __restrict src1,
    size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        __m128i uv = _mm_unpacklo_epi8(
            _mm_movpi64_epi64(src0[i]),
            _mm_movpi64_epi64(src1[i]));
        _mm_stream_si128(origin0 + i * 2, _mm_unpacklo_epi8(src00[i], uv));
        _mm_stream_si128(origin0 + i * 2 + 1, _mm_unpackhi_epi8(src00[i], uv));
        _mm_stream_si128(origin1 + i * 2, _mm_unpacklo_epi8(src01[i], uv));
        _mm_stream_si128(origin1 + i * 2 + 1, _mm_unpackhi_epi8(src01[i], uv));
    }
    _mm_empty();
}

void CopyAndConvert(
    uint32_t* __restrict origin0,
    uint32_t* __restrict origin1,
    const uint8_t* __restrict src00,
    const uint8_t* __restrict src01,
    const uint8_t* __restrict src0,
    const uint8_t* __restrict src1,
    size_t count)
{
    if (!((intptr_t(origin0) & 15) || (intptr_t(origin1) & 15)
        || (intptr_t(src00) & 15) || (intptr_t(src01) & 15)
        || (intptr_t(src0) & 7) || (intptr_t(src1) & 7)))
    {
        const auto simdCount = count / 8;

        SimdCopyAndConvert(
            (__m128i*) origin0,
            (__m128i*) origin1,
            (const __m128i*) src00,
            (const __m128i*) src01,
            (const __m64*) src0,
            (const __m64*) src1,
            simdCount);

        origin0 += simdCount * 8;
        origin1 += simdCount * 8;
        src00 += simdCount * 16;
        src01 += simdCount * 16;
        src0 += simdCount * 8;
        src1 += simdCount * 8;

        count -= simdCount * 8;
    }

    for (unsigned int j = 0; j < count; ++j)
    {
        const uint32_t uv = (src0[j] << 8) | (src1[j] << 24);
        origin0[j] = uv | src00[j * 2] | (src00[j * 2 + 1] << 16);
        origin1[j] = uv | src01[j * 2] | (src01[j * 2 + 1] << 16);
    }
}

} // namespace


class FrameListener : public IFrameListener
{
public:
    explicit FrameListener(CPlayerView* playerView) : m_playerView(playerView) {}

private:
    void updateFrame() override
    {
        m_playerView->updateFrame();
    }
    void drawFrame(IFrameDecoder* decoder) override
    {
        m_playerView->ProcessVideo();
        //m_playerView->Invalidate();
	    decoder->finishedDisplayingFrame();
    }

private:
    CPlayerView* m_playerView;
};

// CPlayerView

IMPLEMENT_DYNCREATE(CPlayerView, CView)

CPlayerView::CPlayerView()
: m_frameListener(new FrameListener(this))
, m_aspectRatio(1, 1)
{
}

CPlayerView::~CPlayerView()
{
}

BEGIN_MESSAGE_MAP(CPlayerView, CView)
    ON_WM_PAINT()
    ON_WM_CREATE()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()



bool CPlayerView::InitializeD3D9()
{
    m_pD3D9.Attach(Direct3DCreate9(D3D_SDK_VERSION));

    if (!m_pD3D9)
    {
        TRACE("Direct3DCreate9 failed.\n");
        return false;
    }

    auto D3DPP = GetD3dPresentParams(*this);

    //
    // First try to create a hardware D3D9 device.
    //
    HRESULT hr = m_pD3D9->CreateDevice(D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        m_hWnd,
        D3DCREATE_FPU_PRESERVE |
        D3DCREATE_MULTITHREADED |
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &D3DPP,
        &m_pD3DD9);

    if (FAILED(hr))
    {
        TRACE("CreateDevice(HAL) failed with error 0x%x.\n", hr);
    }

    //
    // Next try to create a software D3D9 device.
    //
    if (!m_pD3DD9)
    {
        RegisterSoftwareRasterizer(m_pD3D9);

        hr = m_pD3D9->CreateDevice(D3DADAPTER_DEFAULT,
            D3DDEVTYPE_SW,
            m_hWnd,
            D3DCREATE_FPU_PRESERVE |
            D3DCREATE_MULTITHREADED |
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &D3DPP,
            &m_pD3DD9);

        if (FAILED(hr))
        {
            TRACE("CreateDevice(SW) failed with error 0x%x.\n", hr);
        }
    }

    if (!m_pD3DD9)
    {
        return false;
    }


    return true;
}

#ifdef USE_DXVA2
bool CPlayerView::CreateDXVA2VPDevice(REFGUID guid, bool bDXVA2SW, bool createSurface)
{
    //
    // Query the supported render target format.
    //
    UINT i, count;
    D3DFORMAT* formats = NULL;

    auto videoDesc = GetVideoDesc(m_sourceSize);

    HRESULT hr = m_pDXVAVPS->GetVideoProcessorRenderTargets(guid,
        &videoDesc,
        &count,
        &formats);

    if (FAILED(hr))
    {
        TRACE("GetVideoProcessorRenderTargets failed with error 0x%x.\n", hr);
        return false;
    }

    for (i = 0; i < count; ++i)
    {
        if (formats[i] == VIDEO_RENDER_TARGET_FORMAT)
        {
            break;
        }
    }

    CoTaskMemFree(formats);

    if (i >= count)
    {
        TRACE("GetVideoProcessorRenderTargets doesn't support that format.\n");
        return false;
    }

    DXVA2_VideoProcessorCaps g_VPCaps = { 0 };

    //
    // Query video processor capabilities.
    //
    hr = m_pDXVAVPS->GetVideoProcessorCaps(guid,
        &videoDesc,
        VIDEO_RENDER_TARGET_FORMAT,
        &g_VPCaps);

    if (FAILED(hr))
    {
        TRACE("GetVideoProcessorCaps failed with error 0x%x.\n", hr);
        return false;
    }


    //
    // Check to see if the device is software device.
    //
    if (g_VPCaps.DeviceCaps & DXVA2_VPDev_SoftwareDevice)
    {
        if (!bDXVA2SW)
        {
            TRACE("The DXVA2 device isn't a hardware device.\n");
            return false;
        }
    }
    else
    {
        if (bDXVA2SW)
        {
            TRACE("The DXVA2 device isn't a software device.\n");
            return false;
        }
    }

    //
    // This is a progressive device and we cannot provide any reference sample.
    //
    if (g_VPCaps.NumForwardRefSamples > 0 || g_VPCaps.NumBackwardRefSamples > 0)
    {
        TRACE("NumForwardRefSamples or NumBackwardRefSamples is greater than 0.\n");
        return false;
    }

    //
    // Check to see if the device supports all the VP operations we want.
    //
    if ((g_VPCaps.VideoProcessorOperations & VIDEO_REQUIED_OP) != VIDEO_REQUIED_OP)
    {
        TRACE("The DXVA2 device doesn't support the VP operations.\n");
        return false;
    }

    //
    // Create a main stream surface.
    //
    if (createSurface)
    {
        hr = m_pDXVAVPS->CreateSurface(
            (m_sourceSize.cx + 7) & ~7,
            m_sourceSize.cy,
            0,
            VIDEO_MAIN_FORMAT,
            g_VPCaps.InputPool,
            0,
            DXVA2_VideoSoftwareRenderTarget,
            &m_pMainStream,
            NULL);

        if (FAILED(hr))
        {
            TRACE("CreateSurface(MainStream) failed with error 0x%x.\n", hr);
            return false;
        }
    }

    //
    // Query ProcAmp ranges.
    //
    DXVA2_ValueRange range;

    for (i = 0; i < ARRAYSIZE(m_ProcAmpValues); ++i)
    {
        if (g_VPCaps.ProcAmpControlCaps & (1 << i))
        {
            hr = m_pDXVAVPS->GetProcAmpRange(guid,
                &videoDesc,
                VIDEO_RENDER_TARGET_FORMAT,
                1 << i,
                &range);

            if (FAILED(hr))
            {
                TRACE("GetProcAmpRange failed with error 0x%x.\n", hr);
                return false;
            }

            m_ProcAmpValues[i] = range.DefaultValue.ll;
        }
    }

    //
    // Query Noise Filter ranges.
    //
    if (g_VPCaps.VideoProcessorOperations & DXVA2_VideoProcess_NoiseFilter)
    {
        for (i = 0; i < ARRAYSIZE(m_NFilterValues); ++i)
        {
            hr = m_pDXVAVPS->GetFilterPropertyRange(guid,
                &videoDesc,
                VIDEO_RENDER_TARGET_FORMAT,
                DXVA2_NoiseFilterLumaLevel + i,
                &range);

            if (FAILED(hr))
            {
                TRACE("GetFilterPropertyRange(Noise) failed with error 0x%x.\n", hr);
                return false;
            }

            m_NFilterValues[i] = range.DefaultValue.ll;
        }
    }

    //
    // Query Detail Filter ranges.
    //
    if (g_VPCaps.VideoProcessorOperations & DXVA2_VideoProcess_DetailFilter)
    {
        for (i = 0; i < ARRAYSIZE(m_DFilterValues); ++i)
        {
            hr = m_pDXVAVPS->GetFilterPropertyRange(guid,
                &videoDesc,
                VIDEO_RENDER_TARGET_FORMAT,
                DXVA2_DetailFilterLumaLevel + i,
                &range);

            if (FAILED(hr))
            {
                TRACE("GetFilterPropertyRange(Detail) failed with error 0x%x.\n", hr);
                return false;
            }

            m_DFilterValues[i] = range.DefaultValue.ll;
        }
    }

    //
    // Finally create a video processor device.
    //
    hr = m_pDXVAVPS->CreateVideoProcessor(guid,
        &videoDesc,
        VIDEO_RENDER_TARGET_FORMAT,
        SUB_STREAM_COUNT,
        &m_pDXVAVPD);

    if (FAILED(hr))
    {
        TRACE("CreateVideoProcessor failed with error 0x%x.\n", hr);
        return false;
    }

    return true;
}
#endif

bool CPlayerView::InitializeExtra(bool createSurface)
{
    // Retrieve a back buffer as the video render target.
    HRESULT hr = m_pD3DD9->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_pD3DRT);

    if (FAILED(hr))
    {
        TRACE("GetBackBuffer failed with error 0x%x.\n", hr);
        return false;
    }

#ifdef USE_DXVA2
    // Create DXVA2 Video Processor Service.
    hr = DXVA2CreateVideoService(m_pD3DD9,
        IID_IDirectXVideoProcessorService,
        (VOID**)&m_pDXVAVPS);

    if (FAILED(hr))
    {
        TRACE("DXVA2CreateVideoService failed with error 0x%x.\n", hr);
        return false;
    }

    // Initialize the video descriptor.

    // Query the video processor GUID.
    UINT count;
    GUID* guids = NULL;

    auto videoDesc = GetVideoDesc(m_sourceSize);
    hr = m_pDXVAVPS->GetVideoProcessorDeviceGuids(&videoDesc, &count, &guids);

    if (FAILED(hr))
    {
        TRACE("GetVideoProcessorDeviceGuids failed with error 0x%x.\n", hr);
        return false;
    }

    // Create a DXVA2 device.
    bool created = false;
    for (UINT i = 0; i < count; ++i)
    {
        if (CreateDXVA2VPDevice(guids[i], false, createSurface))
        {
            created = true;
            break;
        }
    }
    if (!created)
    {
        for (UINT i = 0; i < count; ++i)
        {
            if (CreateDXVA2VPDevice(guids[i], true, createSurface))
            {
                created = true;
                break;
            }
        }
    }

    CoTaskMemFree(guids);

    if (!m_pDXVAVPD)
    {
        TRACE("Failed to create a DXVA2 device.\n");
        return false;
    }
#else
    if (createSurface)
    {
        hr = m_pD3DD9->CreateOffscreenPlainSurface(
            (m_sourceSize.cx + 7) & ~7,
            m_sourceSize.cy,
            VIDEO_MAIN_FORMAT,
            D3DPOOL_DEFAULT,
            &m_pMainStream,
            NULL);
        if (FAILED(hr))
        {
            TRACE("CreateOffscreenPlainSurface failed with error 0x%x.\n", hr);
            return false;
        }
    }
#endif

    m_subtitleFont = std::make_unique<CD3DFont>(
        _T("MS Sans Serif"),
        max(m_sourceSize.cx / 50, 9));
    m_subtitleFont->InitDeviceObjects(m_pD3DD9);
    m_subtitleFont->RestoreDeviceObjects();
    return true;
}

void CPlayerView::DestroyExtra()
{
    if (m_subtitleFont)
    {
        m_subtitleFont->InvalidateDeviceObjects();
        m_subtitleFont->DeleteDeviceObjects();
        m_subtitleFont.reset();
    }

    m_pMainStream.Release();
#ifdef USE_DXVA2
    m_pDXVAVPD.Release();
    m_pDXVAVPS.Release();
#endif
    m_pD3DRT.Release();
}

void CPlayerView::DestroyD3D9()
{
    m_pD3DD9.Release();
    m_pD3D9.Release();
}


bool CPlayerView::ResetDevice()
{
    bool fullInitialization = true;

    if (m_pD3DD9)
    {
        //
        // Destroy DXVA2 device because it may be holding any D3D9 resources.
        //
        DestroyExtra();

        //
        // Reset will change the parameters, so use a copy instead.
        //
        auto d3dpp = GetD3dPresentParams(*this);

        HRESULT hr = m_pD3DD9->Reset(&d3dpp);

        if (FAILED(hr))
        {
            TRACE("Reset failed with error 0x%x.\n", hr);
        }

        if (SUCCEEDED(hr) && InitializeExtra(true))
        {
            fullInitialization = false;
        }
        else
        {
            //
            // If either Reset didn't work or failed to initialize DXVA2 device,
            // try to recover by recreating the devices from the scratch.
            //
            DestroyExtra();
            DestroyD3D9();
        }
    }

    if (fullInitialization && (!InitializeD3D9() || !InitializeExtra(true)))
    {
        return false;
    }

    return true;
}


CRect CPlayerView::GetScreenPosition()
{
    CRect desc;
    GetClientRect(&desc);

    long long aspectFrameX(m_sourceSize.cx * m_aspectRatio.cx);
    long long aspectFrameY(m_sourceSize.cy * m_aspectRatio.cy);

    CRect target;
    if (aspectFrameY * desc.Width() > aspectFrameX * desc.Height())
    {
        target.top = 0;
        target.bottom = desc.Height();
        LONG width = LONG(aspectFrameX * desc.Height() / aspectFrameY);
        LONG offset = (desc.Width() - width) / 2;
        target.left = offset;
        target.right = width + offset;
    }
    else
    {
        target.left = 0;
        target.right = desc.Width();
        LONG height = LONG(aspectFrameY * desc.Width() / aspectFrameX);
        LONG offset = (desc.Height() - height) / 2;
        target.top = offset;
        target.bottom = height + offset;
    }

    return target;
}


// CPlayerView drawing

bool CPlayerView::ProcessVideo()
{
    if (!m_pD3DD9)
    {
        return false;
    }

    CSingleLock lock(&m_csSurface, TRUE);

    //
    // Check the current status of D3D9 device.
    //
    HRESULT hr = m_pD3DD9->TestCooperativeLevel();
    switch (hr)
    {
    case D3D_OK:
        break;

    case D3DERR_DEVICELOST:
        TRACE("TestCooperativeLevel returned D3DERR_DEVICELOST.\n");
        return true;

    case D3DERR_DEVICENOTRESET:
        TRACE("TestCooperativeLevel returned D3DERR_DEVICENOTRESET.\n");

        if (!m_pD3D9)
        {
            DestroyExtra();
            DestroyD3D9();
            GetDocument()->getFrameDecoder()->videoReset();
            return false;
        }
        else if (!ResetDevice())
        {
            return false;
        }

        break;

    default:
        TRACE("TestCooperativeLevel failed with error 0x%x.\n", hr);
        return false;
    }

    RECT srcRect = { 0, 0, m_sourceSize.cx, m_sourceSize.cy };
    CRect screenPosition = GetScreenPosition();
    CRect target(POINT{}, screenPosition.Size());


#ifdef USE_DXVA2
    const auto blt = GetVideoProcessBltParams(
        target,
        m_ProcAmpValues,
        m_NFilterValues,
        m_DFilterValues);
    const auto sample = GetVideoSample(m_sourceSize, target, m_pMainStream);

    hr = m_pDXVAVPD->VideoProcessBlt(m_pD3DRT,
        &blt,
        &sample,
        SUB_STREAM_COUNT + 1,
        NULL);
    if (FAILED(hr))
    {
        TRACE("VideoProcessBlt failed with error 0x%x.\n", hr);
    }
#else
    m_pD3DD9->Clear(
        0,
        NULL,
        D3DCLEAR_TARGET,
        D3DCOLOR_XRGB(0, 0, 0),
        1.0f,
        0);

    RECT rect{ 0, 0, m_sourceSize.cx, m_sourceSize.cy };

    hr = m_pD3DD9->StretchRect(
        //data.surface ? data.surface : m_pMainStream,
        m_pMainStream,
        &srcRect,
        m_pD3DRT,
        &target,
        D3DTEXF_NONE);
    if (FAILED(hr))
    {
        TRACE("StretchRect failed with error 0x%x.\n", hr);
    }
#endif

    auto subtitle = GetDocument()->getSubtitle();
    if (!subtitle.empty())
    {
        const auto& convertedSubtitle = CA2T(subtitle.c_str(), CP_UTF8);
        hr = m_pD3DD9->BeginScene();
        if (SUCCEEDED(hr))
        {
            CSize boundingBox;
            m_subtitleFont->GetTextExtent(convertedSubtitle, &boundingBox);
            const CSize frameSize = target.Size();
            const auto left = (frameSize.cx - boundingBox.cx) / 2;
            const auto top = frameSize.cy - boundingBox.cy - 2;
            m_subtitleFont->DrawText(left + 1, top + 1, D3DCOLOR_XRGB(0, 0, 0), convertedSubtitle);
            m_subtitleFont->DrawText(left, top, D3DCOLOR_XRGB(255, 255, 255), convertedSubtitle);
            m_pD3DD9->EndScene();
        }
    }


    hr = m_pD3DD9->Present(&target, &screenPosition, GetSafeHwnd(), NULL);
    if (FAILED(hr))
    {
        TRACE("Present failed with error 0x%x.\n", hr);
    }

    return true;
}


void CPlayerView::OnDraw(CDC* /*pDC*/)
{
    //CDocument* pDoc = GetDocument();
    // TODO: add draw code here
}


// CPlayerView diagnostics

#ifdef _DEBUG
void CPlayerView::AssertValid() const
{
    CView::AssertValid();
}

#ifndef _WIN32_WCE
void CPlayerView::Dump(CDumpContext& dc) const
{
    CView::Dump(dc);
}
#endif
#endif //_DEBUG

CPlayerDoc* CPlayerView::GetDocument() const
{
    ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CPlayerDoc)));
    return static_cast<CPlayerDoc*>(m_pDocument);
}


// CPlayerView message handlers


void CPlayerView::OnPaint()
{
    if (!m_pD3DD9)
    {
        __super::OnPaint();
    }
    else
    {
        ProcessVideo();
        ValidateRect(NULL);
    }
}


BOOL CPlayerView::PreCreateWindow(CREATESTRUCT& cs)
{
    // For the full screen mode
    cs.style &= ~WS_BORDER;
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;

    return CView::PreCreateWindow(cs);
}


int CPlayerView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (!InitializeModule())
        return -1;

    if (CView::OnCreate(lpCreateStruct) == -1)
        return -1;

    GetDocument()->getFrameDecoder()->setFrameListener(m_frameListener.get());
    GetDocument()->getFrameDecoder()->SetFrameFormat(IFrameDecoder::
#ifdef CONVERT_FROM_YUV420P
        PIX_FMT_YUV420P
#else
        PIX_FMT_YUYV422
#endif
        , true);

    return 0;
}

void CPlayerView::updateFrame()
{
    CSingleLock lock(&m_csSurface, TRUE);

    FrameRenderingData data;
    if (!GetDocument()->getFrameDecoder()->getFrameRenderingData(&data))
    {
        return;
    }

    m_aspectRatio.cx = data.aspectNum;
    m_aspectRatio.cy = data.aspectDen;

    if (data.d3d9device)
    {
        if (data.d3d9device != m_pD3DD9)
        {
            m_sourceSize.cx = data.width;
            m_sourceSize.cy = data.height;

            DestroyExtra();
            DestroyD3D9();

            m_pD3DD9 = data.d3d9device;
            m_pD3D9.Release();

            InitializeExtra(false);
        }
    }
    else if (!m_pD3D9 || data.width != m_sourceSize.cx || data.height != m_sourceSize.cy)
    {
        m_sourceSize.cx = data.width;
        m_sourceSize.cy = data.height;
        ResetDevice();
    }

    if (data.surface)
    {
        std::swap(m_pMainStream.p, *data.surface);
    }
    else
    {
        D3DLOCKED_RECT lr;
        HRESULT hr = m_pMainStream->LockRect(&lr, NULL, D3DLOCK_NOSYSLOCK);
        if (FAILED(hr))
        {
            TRACE("LockRect failed with error 0x%x.\n", hr);
            return;
        }

#ifdef CONVERT_FROM_YUV420P
        for (int i = 0; i < data.height / 2; ++i)
        {
            CopyAndConvert(
                (uint32_t*)((char*)lr.pBits + lr.Pitch * 2 * i),
                (uint32_t*)((char*)lr.pBits + lr.Pitch * (2 * i + 1)),
                data.image[0] + data.pitch[0] * 2 * i,
                data.image[0] + data.pitch[0] * (2 * i + 1),
                data.image[1] + data.pitch[1] * i,
                data.image[2] + data.pitch[2] * i,
                data.width / 2);
        }
#else
        const size_t lineSize = (size_t)min(lr.Pitch, data.width * 2);
        for (int i = 0; i < data.height; ++i)
        {
            memcpy((BYTE*)lr.pBits + lr.Pitch * i, data.image[0] + data.width * 2 * i, lineSize);
        }
#endif

        hr = m_pMainStream->UnlockRect();
        if (FAILED(hr))
        {
            TRACE("UnlockRect failed with error 0x%x.\n", hr);
        }
    }
}


BOOL CPlayerView::OnEraseBkgnd(CDC* pDC)
{
    if (!m_pD3DD9)
    {
        // Save old brush
        CGdiObject* pOldBrush = pDC->SelectStockObject(BLACK_BRUSH);

        CRect rect;
        pDC->GetClipBox(&rect);     // Erase the area needed

        pDC->PatBlt(rect.left, rect.top, rect.Width(), rect.Height(),
            PATCOPY);
        pDC->SelectObject(pOldBrush);
    }
    return TRUE;
}

void CPlayerView::OnErase(CWnd* pInitiator, CDC* pDC, BOOL isFullScreen)
{
    if (!!m_pD3DD9)
    {
        CSingleLock lock(&m_csSurface, TRUE);

        CRect rect;
        if (isFullScreen)
        {
            pDC->GetClipBox(&rect);     // Erase the area needed
        }
        else
        {
            GetClientRect(&rect);
            MapWindowPoints(pInitiator, &rect);
        }
        CRect targetRect = GetScreenPosition();
        targetRect.DeflateRect(1, 1);
        MapWindowPoints(pInitiator, &targetRect);

        CRgn clientRgn;
        VERIFY(clientRgn.CreateRectRgnIndirect(&rect));
        CRgn targetRgn;
        VERIFY(targetRgn.CreateRectRgnIndirect(&targetRect));
        CRgn combined;
        VERIFY(combined.CreateRectRgnIndirect(&rect));
        VERIFY(combined.CombineRgn(&clientRgn, &targetRgn, RGN_DIFF) != ERROR);

        // Save old brush
        CGdiObject* pOldBrush = pDC->SelectStockObject(BLACK_BRUSH);
        pDC->PaintRgn(&combined);
        pDC->SelectObject(pOldBrush);
    }
}


void CPlayerView::OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint)
{
    if (lHint == UPDATE_HINT_CLOSING)
    {
        {
            CSingleLock lock(&m_csSurface, TRUE);
            DestroyExtra();
            DestroyD3D9();
            m_sourceSize = {};
        }
        RedrawWindow();
    }

    __super::OnUpdate(pSender, lHint, pHint);
}
