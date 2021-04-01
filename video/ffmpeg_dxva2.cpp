/*
* This file is part of FFmpeg.
*
* FFmpeg is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* FFmpeg is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/


#include <d3d9.h>
#include <dxva2api.h>

#include "ffmpeg_dxva2.h"

extern "C"
{

#include "libavcodec/dxva2.h"

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"

#include "libavutil/cpu.h"
}

#include <emmintrin.h>
#include <smmintrin.h>

#include <algorithm>


// https://github.com/NVIDIA/gdrcopy/blob/master/memcpy_sse41.c

// implementation of copy from BAR using MOVNTDQA 
// suggested by Nicholas Wilt <nwilt@amazon.com>

// src is WC MMIO of GPU BAR
// dest is host memory
static int memcpy_uncached_load_sse41(void *dest, const void *src, size_t n_bytes)
{
    int ret = 0;

    char *d = (char*)dest;
    uintptr_t d_int = (uintptr_t)d;
    const char *s = (const char *)src;
    uintptr_t s_int = (uintptr_t)s;
    size_t n = n_bytes;

    // align src to 128-bits
    if (s_int & 0xf) {
        size_t nh = std::min(0x10 - (s_int & 0x0f), n);
        memcpy(d, s, nh);
        d += nh; d_int += nh;
        s += nh; s_int += nh;
        n -= nh;
    }

    if (d_int & 0xf) { // dest is not aligned to 128-bits
        __m128i r0, r1, r2, r3, r4, r5, r6, r7;
        // unroll 8
        while (n >= 8 * sizeof(__m128i)) {
            r0 = _mm_stream_load_si128((__m128i *)(s + 0 * sizeof(__m128i)));
            r1 = _mm_stream_load_si128((__m128i *)(s + 1 * sizeof(__m128i)));
            r2 = _mm_stream_load_si128((__m128i *)(s + 2 * sizeof(__m128i)));
            r3 = _mm_stream_load_si128((__m128i *)(s + 3 * sizeof(__m128i)));
            r4 = _mm_stream_load_si128((__m128i *)(s + 4 * sizeof(__m128i)));
            r5 = _mm_stream_load_si128((__m128i *)(s + 5 * sizeof(__m128i)));
            r6 = _mm_stream_load_si128((__m128i *)(s + 6 * sizeof(__m128i)));
            r7 = _mm_stream_load_si128((__m128i *)(s + 7 * sizeof(__m128i)));
            _mm_storeu_si128((__m128i *)(d + 0 * sizeof(__m128i)), r0);
            _mm_storeu_si128((__m128i *)(d + 1 * sizeof(__m128i)), r1);
            _mm_storeu_si128((__m128i *)(d + 2 * sizeof(__m128i)), r2);
            _mm_storeu_si128((__m128i *)(d + 3 * sizeof(__m128i)), r3);
            _mm_storeu_si128((__m128i *)(d + 4 * sizeof(__m128i)), r4);
            _mm_storeu_si128((__m128i *)(d + 5 * sizeof(__m128i)), r5);
            _mm_storeu_si128((__m128i *)(d + 6 * sizeof(__m128i)), r6);
            _mm_storeu_si128((__m128i *)(d + 7 * sizeof(__m128i)), r7);
            s += 8 * sizeof(__m128i);
            d += 8 * sizeof(__m128i);
            n -= 8 * sizeof(__m128i);
        }
        while (n >= sizeof(__m128i)) {
            r0 = _mm_stream_load_si128((__m128i *)(s + 0 * sizeof(__m128i)));
            _mm_storeu_si128((__m128i *)(d + 0 * sizeof(__m128i)), r0);
            s += sizeof(__m128i);
            d += sizeof(__m128i);
            n -= sizeof(__m128i);
        }
    }
    else { // or it IS aligned
        __m128i r0, r1, r2, r3, r4, r5, r6, r7;
        // unroll 8
        while (n >= 8 * sizeof(__m128i)) {
            r0 = _mm_stream_load_si128((__m128i *)(s + 0 * sizeof(__m128i)));
            r1 = _mm_stream_load_si128((__m128i *)(s + 1 * sizeof(__m128i)));
            r2 = _mm_stream_load_si128((__m128i *)(s + 2 * sizeof(__m128i)));
            r3 = _mm_stream_load_si128((__m128i *)(s + 3 * sizeof(__m128i)));
            r4 = _mm_stream_load_si128((__m128i *)(s + 4 * sizeof(__m128i)));
            r5 = _mm_stream_load_si128((__m128i *)(s + 5 * sizeof(__m128i)));
            r6 = _mm_stream_load_si128((__m128i *)(s + 6 * sizeof(__m128i)));
            r7 = _mm_stream_load_si128((__m128i *)(s + 7 * sizeof(__m128i)));
            _mm_stream_si128((__m128i *)(d + 0 * sizeof(__m128i)), r0);
            _mm_stream_si128((__m128i *)(d + 1 * sizeof(__m128i)), r1);
            _mm_stream_si128((__m128i *)(d + 2 * sizeof(__m128i)), r2);
            _mm_stream_si128((__m128i *)(d + 3 * sizeof(__m128i)), r3);
            _mm_stream_si128((__m128i *)(d + 4 * sizeof(__m128i)), r4);
            _mm_stream_si128((__m128i *)(d + 5 * sizeof(__m128i)), r5);
            _mm_stream_si128((__m128i *)(d + 6 * sizeof(__m128i)), r6);
            _mm_stream_si128((__m128i *)(d + 7 * sizeof(__m128i)), r7);
            s += 8 * sizeof(__m128i);
            d += 8 * sizeof(__m128i);
            n -= 8 * sizeof(__m128i);
        }
        while (n >= sizeof(__m128i)) {
            r0 = _mm_stream_load_si128((__m128i *)(s + 0 * sizeof(__m128i)));
            _mm_stream_si128((__m128i *)(d + 0 * sizeof(__m128i)), r0);
            s += sizeof(__m128i);
            d += sizeof(__m128i);
            n -= sizeof(__m128i);
        }
    }

    if (n)
        memcpy(d, s, n);

    // fencing because of NT stores
    // potential optimization: issue only when NT stores are actually emitted
    _mm_sfence();

    return ret;
}


static void CopyPlane(uint8_t *dst, int dst_linesize,
                    const uint8_t *src, int src_linesize,
                    int bytewidth, int height)
{
    if (!dst || !src)
        return;

    for (;height > 0; height--) {
        memcpy_uncached_load_sse41(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }
}


// TODO use better criteria
static intptr_t getHWAccelDevice(IDirect3D9* pDirect3D9)
{
    intptr_t result = D3DADAPTER_DEFAULT;
    const auto count = pDirect3D9->GetAdapterCount();
    UINT height{}, width{};
    for (unsigned int i = 0; i < count; ++i)
    {
        D3DDISPLAYMODE d3ddm{};
        if (SUCCEEDED(pDirect3D9->GetAdapterDisplayMode(i, &d3ddm))
            && d3ddm.Height > height && d3ddm.Width > width)
        {
            result = i;
            height = d3ddm.Height;
            width = d3ddm.Width;
        }
    }

    return result;
}


    /* define all the GUIDs used directly here,
    to avoid problems with inconsistent dxva2api.h versions in mingw-w64 and different MSVC version */
#include <initguid.h>

// https://docs.microsoft.com/windows-hardware/drivers/display/providing-capabilities-for-video-decoding

DEFINE_GUID(IID_IDirectXVideoDecoderService, 0xfc51a551, 0xd5e7, 0x11d9, 0xaf, 0x55, 0x00, 0x05, 0x4e, 0x43, 0xff, 0x02);

DEFINE_GUID(DXVA2_ModeMPEG2_VLD, 0xee27417f, 0x5e28, 0x4e65, 0xbe, 0xea, 0x1d, 0x26, 0xb5, 0x08, 0xad, 0xc9);
DEFINE_GUID(DXVA2_ModeMPEG2and1_VLD, 0x86695f12, 0x340e, 0x4f04, 0x9f, 0xd3, 0x92, 0x53, 0xdd, 0x32, 0x74, 0x60);
DEFINE_GUID(DXVA2_ModeMPEG2_MoComp, 0xe6a9f44b, 0x61b0, 0x4563, 0x9e, 0xa4, 0x63, 0xd2, 0xa3, 0xc6, 0xfe, 0x66);
DEFINE_GUID(DXVADDI_ModeMPEG2_IDCT, 0xbf22ad00, 0x03ea, 0x4690, 0x80, 0x77, 0x47, 0x33, 0x46, 0x20, 0x9b, 0x7e);

DEFINE_GUID(DXVA2_ModeH264_E, 0x1b81be68, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA2_ModeH264_F, 0x1b81be69, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_Intel_ModeH264_E, 0x604F8E68, 0x4951, 0x4C54, 0x88, 0xFE, 0xAB, 0xD2, 0x5C, 0x15, 0xB3, 0xD6);

DEFINE_GUID(DXVADDI_ModeH264_A, 0x1b81be64, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeH264_B, 0x1b81be65, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeH264_C, 0x1b81be66, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeH264_D, 0x1b81be67, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVA2_ModeVC1_D, 0x1b81beA3, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA2_ModeVC1_D2010, 0x1b81beA4, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVA2_ModeHEVC_VLD_Main, 0x5b11d51b, 0x2f4c, 0x4452, 0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0);
DEFINE_GUID(DXVA2_ModeHEVC_VLD_Main10,0x107af0e0, 0xef1a,0x4d19,0xab,0xa8,0x67,0xa1,0x63,0x07,0x3d,0x13);
DEFINE_GUID(DXVA2_ModeVP9_VLD_Profile0, 0x463707f8, 0xa1d0, 0x4585, 0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e);
DEFINE_GUID(DXVA2_ModeVP9_VLD_10bit_Profile2, 0xa4c749ef, 0x6ecf, 0x48aa, 0x84, 0x48, 0x50, 0xa7, 0xa1, 0x16, 0x5f, 0xf7);

DEFINE_GUID(DXVADDI_ModeVC1_A, 0x1b81beA0, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeVC1_B, 0x1b81beA1, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeVC1_C, 0x1b81beA2, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVADDI_ModeWMV8_A, 0x1b81be80, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeWMV8_B, 0x1b81be81, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVADDI_ModeWMV9_A, 0x1b81be90, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeWMV9_B, 0x1b81be91, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVADDI_ModeWMV9_C, 0x1b81be94, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVA2_NoEncrypt, 0x1b81beD0, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(GUID_NULL, 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

//DEFINE_GUID(DXVA2_Unknown, 0xA74CCAE2, 0xF466, 0x45AE, 0x86, 0xF5, 0xAB, 0x8B, 0xE8, 0xAF, 0x84, 0x83);

typedef IDirect3D9* WINAPI pDirect3DCreate9(UINT);
typedef HRESULT WINAPI pCreateDeviceManager9(UINT *, IDirect3DDeviceManager9 **);

typedef struct dxva2_mode {
    const GUID     *guid;
    enum AVCodecID codec;
} dxva2_mode;

static const dxva2_mode dxva2_modes[] = {
    /* MPEG-2 */
    { &DXVA2_ModeMPEG2_VLD, AV_CODEC_ID_MPEG2VIDEO },
    { &DXVA2_ModeMPEG2and1_VLD, AV_CODEC_ID_MPEG2VIDEO },

    { &DXVA2_ModeMPEG2_MoComp, AV_CODEC_ID_MPEG2VIDEO },
    { &DXVADDI_ModeMPEG2_IDCT, AV_CODEC_ID_MPEG2VIDEO },

    /* H.264 */
    { &DXVA2_ModeH264_F, AV_CODEC_ID_H264 },
    { &DXVA2_ModeH264_E, AV_CODEC_ID_H264 },
    /* Intel specific H.264 mode */
    { &DXVADDI_Intel_ModeH264_E, AV_CODEC_ID_H264 },

    { &DXVADDI_ModeH264_A, AV_CODEC_ID_H264 },
    { &DXVADDI_ModeH264_B, AV_CODEC_ID_H264 },
    { &DXVADDI_ModeH264_C, AV_CODEC_ID_H264 },
    { &DXVADDI_ModeH264_D, AV_CODEC_ID_H264 },

    /* VC-1 / WMV3 */
    { &DXVA2_ModeVC1_D2010, AV_CODEC_ID_VC1 },
    { &DXVA2_ModeVC1_D2010, AV_CODEC_ID_WMV3 },
    { &DXVA2_ModeVC1_D, AV_CODEC_ID_VC1 },
    { &DXVA2_ModeVC1_D, AV_CODEC_ID_WMV3 },

    { &DXVADDI_ModeVC1_A, AV_CODEC_ID_VC1 },
    { &DXVADDI_ModeVC1_A, AV_CODEC_ID_WMV3 },
    { &DXVADDI_ModeVC1_B, AV_CODEC_ID_VC1 },
    { &DXVADDI_ModeVC1_B, AV_CODEC_ID_WMV3 },
    { &DXVADDI_ModeVC1_C, AV_CODEC_ID_VC1 },
    { &DXVADDI_ModeVC1_C, AV_CODEC_ID_WMV3 },


    { &DXVADDI_ModeWMV8_A, AV_CODEC_ID_WMV3 },
    { &DXVADDI_ModeWMV8_B, AV_CODEC_ID_WMV3 },
    { &DXVADDI_ModeWMV9_A, AV_CODEC_ID_WMV3 },
    { &DXVADDI_ModeWMV9_B, AV_CODEC_ID_WMV3 },
    { &DXVADDI_ModeWMV9_C, AV_CODEC_ID_WMV3 },


    /* HEVC/H.265 */
    { &DXVA2_ModeHEVC_VLD_Main10,AV_CODEC_ID_HEVC }, // comes first
    { &DXVA2_ModeHEVC_VLD_Main, AV_CODEC_ID_HEVC },

    /* VP8/9 */
    { &DXVA2_ModeVP9_VLD_10bit_Profile2, AV_CODEC_ID_VP9 },
    { &DXVA2_ModeVP9_VLD_Profile0, AV_CODEC_ID_VP9 },

    { NULL, AV_CODEC_ID_NONE },
};

typedef struct surface_info {
    int used;
    uint64_t age;
} surface_info;

typedef struct DXVA2Context {
    HMODULE d3dlib;
    HMODULE dxva2lib;

    HANDLE  deviceHandle;

    IDirect3D9                  *d3d9;
    IDirect3DDevice9            *d3d9device;
    IDirect3DDeviceManager9     *d3d9devmgr;
    IDirectXVideoDecoderService *decoder_service;
    IDirectXVideoDecoder        *decoder;

    GUID                        decoder_guid;
    DXVA2_ConfigPictureDecode   decoder_config;

    LPDIRECT3DSURFACE9          *surfaces;
    surface_info                *surface_infos;
    uint32_t                    num_surfaces;
    uint64_t                    surface_age;

    AVFrame                     *tmp_frame;
} DXVA2Context;

typedef struct DXVA2SurfaceWrapper {
    DXVA2Context         *ctx;
    LPDIRECT3DSURFACE9   surface;
    IDirectXVideoDecoder *decoder;
} DXVA2SurfaceWrapper;

static void dxva2_destroy_decoder(AVCodecContext *s)
{
    InputStream  *ist = (InputStream *)s->opaque;
    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;

    if (ctx->surfaces) {
        for (int i = 0; i < ctx->num_surfaces; i++) {
            if (ctx->surfaces[i])
                IDirect3DSurface9_Release(ctx->surfaces[i]);
        }
    }
    av_freep(&ctx->surfaces);
    av_freep(&ctx->surface_infos);
    ctx->num_surfaces = 0;
    ctx->surface_age = 0;

    if (ctx->decoder) {
        ctx->decoder->Release();
        ctx->decoder = NULL;
    }
}

static void dxva2_uninit(AVCodecContext *s)
{
    InputStream  *ist = (InputStream  *)s->opaque;
    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;

    ist->hwaccel_uninit = NULL;
    ist->hwaccel_get_buffer = NULL;
    ist->hwaccel_retrieve_data = NULL;

    if (ctx->decoder)
        dxva2_destroy_decoder(s);

    if (ctx->decoder_service)
        ctx->decoder_service->Release();

    if (ctx->d3d9devmgr && ctx->deviceHandle != INVALID_HANDLE_VALUE)
        ctx->d3d9devmgr->CloseDeviceHandle(ctx->deviceHandle);

    if (ctx->d3d9devmgr)
        ctx->d3d9devmgr->Release();

    if (ctx->d3d9device)
        IDirect3DDevice9_Release(ctx->d3d9device);

    if (ctx->d3d9)
        IDirect3D9_Release(ctx->d3d9);

    if (ctx->d3dlib)
        FreeLibrary(ctx->d3dlib);

    if (ctx->dxva2lib)
        FreeLibrary(ctx->dxva2lib);

    av_frame_free(&ctx->tmp_frame);

    av_freep(&ist->hwaccel_ctx);
    av_freep(&s->hwaccel_context);
}

static void dxva2_release_buffer(void *opaque, uint8_t *data)
{
    DXVA2SurfaceWrapper *w = (DXVA2SurfaceWrapper *)opaque;
    DXVA2Context        *ctx = w->ctx;
    int i;

    for (i = 0; i < ctx->num_surfaces; i++) {
        if (ctx->surfaces[i] == w->surface) {
            ctx->surface_infos[i].used = 0;
            break;
        }
    }
    IDirect3DSurface9_Release(w->surface);
    w->decoder->Release();
    av_free(w);
}

static int dxva2_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream  *ist = (InputStream  *)s->opaque;
    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;
    int i, old_unused = -1;
    LPDIRECT3DSURFACE9 surface;
    DXVA2SurfaceWrapper *w = NULL;

    av_assert0(frame->format == AV_PIX_FMT_DXVA2_VLD);

    for (i = 0; i < ctx->num_surfaces; i++) {
        surface_info *info = &ctx->surface_infos[i];
        if (!info->used && (old_unused == -1 || info->age < ctx->surface_infos[old_unused].age))
            old_unused = i;
    }
    if (old_unused == -1) {
        av_log(NULL, AV_LOG_ERROR, "No free DXVA2 surface!\n");
        return AVERROR(ENOMEM);
    }
    i = old_unused;

    surface = ctx->surfaces[i];

    w = (DXVA2SurfaceWrapper *)av_mallocz(sizeof(*w));
    if (!w)
        return AVERROR(ENOMEM);

    frame->buf[0] = av_buffer_create((uint8_t*)surface, 0,
        dxva2_release_buffer, w,
        AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        av_free(w);
        return AVERROR(ENOMEM);
    }

    w->ctx = ctx;
    w->surface = surface;
    IDirect3DSurface9_AddRef(w->surface);
    w->decoder = ctx->decoder;
    w->decoder->AddRef();

    ctx->surface_infos[i].used = 1;
    ctx->surface_infos[i].age = ctx->surface_age++;

    frame->data[3] = (uint8_t *)surface;

    return 0;
}

static const auto CopyPlanePtr = (av_get_cpu_flags() & AV_CPU_FLAG_SSE4) ? CopyPlane : av_image_copy_plane;

int dxva2_convert_data(IDirect3DSurface9* surface, AVFrame *tmp_frame, int width, int height)
{
    D3DSURFACE_DESC    surfaceDesc;
    D3DLOCKED_RECT     LockedRect;
    HRESULT            hr;
    int                ret;

    IDirect3DSurface9_GetDesc(surface, &surfaceDesc);

    hr = IDirect3DSurface9_LockRect(surface, &LockedRect, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) {
        av_log(NULL, AV_LOG_ERROR, "Unable to lock DXVA2 surface\n");
        return AVERROR_UNKNOWN;
    }

    tmp_frame->width = width;
    tmp_frame->height = height;
    switch (surfaceDesc.Format)
    {
    case MKTAG('N', 'V', '1', '2'):
        tmp_frame->format = AV_PIX_FMT_NV12;

        ret = av_frame_get_buffer(tmp_frame, 32);
        if (ret == 0)
        {
            CopyPlanePtr(tmp_frame->data[0], tmp_frame->linesize[0],
                (uint8_t*)LockedRect.pBits,
                LockedRect.Pitch, width, height);

            CopyPlanePtr(tmp_frame->data[1], tmp_frame->linesize[1],
                (uint8_t*)LockedRect.pBits + LockedRect.Pitch * surfaceDesc.Height,
                LockedRect.Pitch, width, height / 2);
        }
        break;
    case MKTAG('P', '0', '1', '0'):
        tmp_frame->format = AV_PIX_FMT_P010;

        ret = av_frame_get_buffer(tmp_frame, 32);
        if (ret == 0)
        {
            CopyPlanePtr(tmp_frame->data[0], tmp_frame->linesize[0],
                (uint8_t*)LockedRect.pBits,
                LockedRect.Pitch, width * 2, height);

            CopyPlanePtr(tmp_frame->data[1], tmp_frame->linesize[1],
                (uint8_t*)LockedRect.pBits + LockedRect.Pitch * surfaceDesc.Height,
                LockedRect.Pitch, width * 2, height / 2);
        }
        break;
    case D3DFMT_YUY2:
        tmp_frame->format = AV_PIX_FMT_YUYV422;

        ret = av_frame_get_buffer(tmp_frame, 32);
        if (ret == 0)
        {
            CopyPlanePtr(tmp_frame->data[0], tmp_frame->linesize[0],
                (uint8_t*)LockedRect.pBits,
                LockedRect.Pitch, width, height * 2);
        }
        break;
    default: // IMC3
        tmp_frame->format = AV_PIX_FMT_YUV420P;

        ret = av_frame_get_buffer(tmp_frame, 32);
        if (ret == 0)
        {
            uint8_t* pU = (uint8_t*)LockedRect.pBits + (((height + 15) & ~15) * LockedRect.Pitch);
            uint8_t* pV = (uint8_t*)LockedRect.pBits + (((((height * 3) / 2) + 15) & ~15) * LockedRect.Pitch);

            CopyPlanePtr(tmp_frame->data[0], tmp_frame->linesize[0],
                (uint8_t*)LockedRect.pBits,
                LockedRect.Pitch, width, height);

            CopyPlanePtr(tmp_frame->data[1], tmp_frame->linesize[1],
                pU, LockedRect.Pitch, width / 2, height / 2);

            CopyPlanePtr(tmp_frame->data[2], tmp_frame->linesize[2],
                pV, LockedRect.Pitch, width / 2, height / 2);
        }
    }

    IDirect3DSurface9_UnlockRect(surface);

    return ret;
}

int dxva2_retrieve_data(AVCodecContext *s, AVFrame *frame)
{
    LPDIRECT3DSURFACE9 surface = (LPDIRECT3DSURFACE9)frame->data[3];
    InputStream        *ist = (InputStream *)s->opaque;
    DXVA2Context       *ctx = (DXVA2Context *)ist->hwaccel_ctx;

    int ret = dxva2_convert_data(surface, ctx->tmp_frame, frame->width, frame->height);
    if (ret < 0)
        return ret;

    ret = av_frame_copy_props(ctx->tmp_frame, frame);
    if (ret < 0)
    {
        av_frame_unref(ctx->tmp_frame);
        return ret;
    }

    av_frame_unref(frame);
    av_frame_move_ref(frame, ctx->tmp_frame);

    return 0;
}

static int dxva2_alloc(AVCodecContext *s)
{
    InputStream *ist = (InputStream *)s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx;
    pDirect3DCreate9      *createD3D = NULL;
    pCreateDeviceManager9 *createDeviceManager = NULL;
    HRESULT hr;
    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    //D3DDISPLAYMODE        d3ddm;
    unsigned resetToken = 0;
    UINT adapter = D3DADAPTER_DEFAULT;

    ctx = (DXVA2Context *)av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->deviceHandle = INVALID_HANDLE_VALUE;

    ist->hwaccel_ctx = ctx;
    ist->hwaccel_uninit = dxva2_uninit;
    ist->hwaccel_get_buffer = dxva2_get_buffer;
    ist->hwaccel_retrieve_data = dxva2_retrieve_data;

    ctx->d3dlib = LoadLibrary(L"d3d9.dll");
    if (!ctx->d3dlib) {
        av_log(NULL, loglevel, "Failed to load D3D9 library\n");
        goto fail;
    }
    ctx->dxva2lib = LoadLibrary(L"dxva2.dll");
    if (!ctx->dxva2lib) {
        av_log(NULL, loglevel, "Failed to load DXVA2 library\n");
        goto fail;
    }

    createD3D = (pDirect3DCreate9 *)GetProcAddress(ctx->d3dlib, "Direct3DCreate9");
    if (!createD3D) {
        av_log(NULL, loglevel, "Failed to locate Direct3DCreate9\n");
        goto fail;
    }
    createDeviceManager = (pCreateDeviceManager9 *)GetProcAddress(ctx->dxva2lib, "DXVA2CreateDirect3DDeviceManager9");
    if (!createDeviceManager) {
        av_log(NULL, loglevel, "Failed to locate DXVA2CreateDirect3DDeviceManager9\n");
        goto fail;
    }

    ctx->d3d9 = createD3D(D3D_SDK_VERSION);
    if (!ctx->d3d9) {
        av_log(NULL, loglevel, "Failed to create IDirect3D object\n");
        goto fail;
    }

    if (ist->hwaccel_device) {
        adapter = ist->hwaccel_device;
    }
    else {
        adapter = getHWAccelDevice(ctx->d3d9);
    }
    av_log(NULL, AV_LOG_INFO, "Using HWAccel device %d\n", adapter);

    //IDirect3D9_GetAdapterDisplayMode(ctx->d3d9, adapter, &d3ddm);
    d3dpp.Windowed = TRUE;
    d3dpp.BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
    d3dpp.BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
    d3dpp.BackBufferCount = 1;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;// d3ddm.Format;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.Flags = D3DPRESENTFLAG_VIDEO;

    d3dpp.Windowed = TRUE;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;


    hr = IDirect3D9_CreateDevice(ctx->d3d9, adapter, D3DDEVTYPE_HAL, GetDesktopWindow(),
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &d3dpp, &ctx->d3d9device);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create Direct3D device\n");
        goto fail;
    }

    hr = createDeviceManager(&resetToken, &ctx->d3d9devmgr);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create Direct3D device manager\n");
        goto fail;
    }

    hr = ctx->d3d9devmgr->ResetDevice(ctx->d3d9device, resetToken);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to bind Direct3D device to device manager\n");
        goto fail;
    }

    hr = ctx->d3d9devmgr->OpenDeviceHandle(&ctx->deviceHandle);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to open device handle\n");
        goto fail;
    }

    hr = ctx->d3d9devmgr->GetVideoService(ctx->deviceHandle, IID_IDirectXVideoDecoderService, (void **)&ctx->decoder_service);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create IDirectXVideoDecoderService\n");
        goto fail;
    }

    ctx->tmp_frame = av_frame_alloc();
    if (!ctx->tmp_frame)
        goto fail;

    s->hwaccel_context = av_mallocz(sizeof(struct dxva_context));
    if (!s->hwaccel_context)
        goto fail;

    return 0;
fail:
    dxva2_uninit(s);
    return AVERROR(EINVAL);
}

static int dxva2_get_decoder_configuration(AVCodecContext *s, const GUID *device_guid,
    const DXVA2_VideoDesc *desc,
    DXVA2_ConfigPictureDecode *config)
{
    InputStream  *ist = (InputStream *)s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;
    unsigned cfg_count = 0, best_score = 0;
    DXVA2_ConfigPictureDecode *cfg_list = NULL;
    DXVA2_ConfigPictureDecode best_cfg = { { 0 } };
    HRESULT hr;
    int i;

    hr = ctx->decoder_service->GetDecoderConfigurations(*device_guid, desc, NULL, &cfg_count, &cfg_list);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Unable to retrieve decoder configurations\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < cfg_count; i++) {
        DXVA2_ConfigPictureDecode *cfg = &cfg_list[i];

        unsigned score;
        if (cfg->ConfigBitstreamRaw == 1)
            score = 1;
        else if (s->codec_id == AV_CODEC_ID_H264 && cfg->ConfigBitstreamRaw == 2)
            score = 2;
        else
            continue;
        if (IsEqualGUID(cfg->guidConfigBitstreamEncryption, DXVA2_NoEncrypt))
            score += 16;
        if (score > best_score) {
            best_score = score;
            best_cfg = *cfg;
        }
    }
    CoTaskMemFree(cfg_list);

    if (!best_score) {
        av_log(NULL, loglevel, "No valid decoder configuration available\n");
        return AVERROR(EINVAL);
    }

    *config = best_cfg;
    return 0;
}

static bool isMoreThan8BytesFmt(AVPixelFormat f)
{
    if (auto d = av_pix_fmt_desc_get(f))
    {
        for (int i = 0; i < d->nb_components; ++i)
            if (d->comp[i].depth > 8)
                return true;
    }
    return false;
}

static int dxva2_create_decoder(AVCodecContext *s)
{
    InputStream  *ist = (InputStream *)s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;
    struct dxva_context *dxva_ctx = (dxva_context *)s->hwaccel_context;
    GUID *guid_list = NULL;
    unsigned guid_count = 0, i, j;
    GUID device_guid = GUID_NULL;
    D3DFORMAT target_format = D3DFMT_UNKNOWN;
    DXVA2_VideoDesc desc = { 0 };
    DXVA2_ConfigPictureDecode config;
    HRESULT hr;
    int surface_alignment;
    int ret;

    hr = ctx->decoder_service->GetDecoderDeviceGuids(&guid_count, &guid_list);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to retrieve decoder device GUIDs\n");
        goto fail;
    }

    const bool is10format = isMoreThan8BytesFmt(
        (s->sw_pix_fmt == AV_PIX_FMT_NONE)? s->pix_fmt : s->sw_pix_fmt);

    for (i = 0; dxva2_modes[i].guid; i++) {
        D3DFORMAT *target_list = NULL;
        unsigned target_count = 0;
        const dxva2_mode *mode = &dxva2_modes[i];
        if (mode->codec != s->codec_id)
            continue;

        for (j = 0; j < guid_count; j++) {
            if (IsEqualGUID(*mode->guid, guid_list[j]))
                break;
        }
        if (j == guid_count)
            continue;

        hr = ctx->decoder_service->GetDecoderRenderTargets(*mode->guid, &target_count, &target_list);
        if (FAILED(hr)) {
            continue;
        }

        for (j = 0; j < target_count; j++) {
            const D3DFORMAT format = target_list[j];
            if (is10format? (format == MKTAG('P', '0', '1', '0'))
                    : (format == MKTAG('N', 'V', '1', '2') || format == MKTAG('I', 'M', 'C', '3'))) {
                target_format = format;
                break;
            }
        }
        CoTaskMemFree(target_list);
        if (target_format) {
            device_guid = *mode->guid;
            break;
        }
    }
    CoTaskMemFree(guid_list);

    if (IsEqualGUID(device_guid, GUID_NULL)) {
        av_log(NULL, loglevel, "No decoder device for codec found\n");
        goto fail;
    }

    desc.SampleWidth = s->coded_width;
    desc.SampleHeight = s->coded_height;
    desc.Format = target_format;

    ret = dxva2_get_decoder_configuration(s, &device_guid, &desc, &config);
    if (ret < 0) {
        goto fail;
    }

    /* decoding MPEG-2 requires additional alignment on some Intel GPUs,
    but it causes issues for H.264 on certain AMD GPUs..... */
    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO)
        surface_alignment = 32;
    /* the HEVC DXVA2 spec asks for 128 pixel aligned surfaces to ensure
    all coding features have enough room to work with */
    else if (s->codec_id == AV_CODEC_ID_HEVC)
        surface_alignment = 128;
    else
        surface_alignment = 16;

    /* 4 base work surfaces */
    //ctx->num_surfaces = 4;
    ctx->num_surfaces = 4 + 3; // two video queue elements and one cached by view

    /* add surfaces based on number of possible refs */
    if (s->codec_id == AV_CODEC_ID_H264 || s->codec_id == AV_CODEC_ID_HEVC)
        ctx->num_surfaces += 16;
    else if (s->codec_id == AV_CODEC_ID_VP9)
        ctx->num_surfaces += 8;
    else
        ctx->num_surfaces += 2;

    /* add extra surfaces for frame threading */
    if (s->active_thread_type & FF_THREAD_FRAME)
        ctx->num_surfaces += s->thread_count;

    ctx->surfaces = (LPDIRECT3DSURFACE9 *)av_mallocz(ctx->num_surfaces * sizeof(*ctx->surfaces));
    ctx->surface_infos = (surface_info *)av_mallocz(ctx->num_surfaces * sizeof(*ctx->surface_infos));

    if (!ctx->surfaces || !ctx->surface_infos) {
        av_log(NULL, loglevel, "Unable to allocate surface arrays\n");
        goto fail;
    }

    hr = ctx->decoder_service->CreateSurface(FFALIGN(s->coded_width, surface_alignment),
        FFALIGN(s->coded_height, surface_alignment),
        ctx->num_surfaces - 1,
        target_format, D3DPOOL_DEFAULT, 0,
        DXVA2_VideoDecoderRenderTarget,
        ctx->surfaces, NULL);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create %d video surfaces\n", ctx->num_surfaces);
        goto fail;
    }

    hr = ctx->decoder_service->CreateVideoDecoder(device_guid,
        &desc, &config, ctx->surfaces,
        ctx->num_surfaces, &ctx->decoder);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create DXVA2 video decoder\n");
        goto fail;
    }

    ctx->decoder_guid = device_guid;
    ctx->decoder_config = config;

    dxva_ctx->cfg = &ctx->decoder_config;
    dxva_ctx->decoder = ctx->decoder;
    dxva_ctx->surface = ctx->surfaces;
    dxva_ctx->surface_count = ctx->num_surfaces;

    if (IsEqualGUID(ctx->decoder_guid, DXVADDI_Intel_ModeH264_E))
        dxva_ctx->workaround |= FF_DXVA2_WORKAROUND_INTEL_CLEARVIDEO;

    return 0;
fail:
    dxva2_destroy_decoder(s);
    return AVERROR(EINVAL);
}

int dxva2_init(AVCodecContext *s)
{
    InputStream *ist = (InputStream *)s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;

    if (s->codec_id == AV_CODEC_ID_H264 &&
        (s->profile & ~FF_PROFILE_H264_CONSTRAINED) > FF_PROFILE_H264_HIGH) {
        av_log(NULL, loglevel, "Unsupported H.264 profile for DXVA2 HWAccel: %d\n", s->profile);
        return AVERROR(EINVAL);
    }

    if (s->codec_id == AV_CODEC_ID_HEVC &&
        s->profile != FF_PROFILE_HEVC_MAIN && s->profile != FF_PROFILE_HEVC_MAIN_10) {
        av_log(NULL, loglevel, "Unsupported HEVC profile for DXVA2 HWAccel: %d\n", s->profile);
        return AVERROR(EINVAL);
    }

    int ret;

    if (!ist->hwaccel_ctx) {
        ret = dxva2_alloc(s);
        if (ret < 0)
            return ret;
    }

    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;

    if (ctx->decoder)
        dxva2_destroy_decoder(s);

    ret = dxva2_create_decoder(s);
    if (ret < 0) {
        av_log(NULL, loglevel, "Error creating the DXVA2 decoder\n");
        dxva2_uninit(s);
        return ret;
    }

    return 0;
}


IDirect3DDevice9* get_device(AVCodecContext *s)
{
    InputStream  *ist = (InputStream *)s->opaque;
    DXVA2Context *ctx = (DXVA2Context *)ist->hwaccel_ctx;
    return ctx->d3d9device;
}
