#include "stdafx.h"
#include "FrameToHglobal.h"

#include "ffmpeg_dxva2.h"

extern "C"
{
#include "libswscale/swscale.h"
};

HGLOBAL FrameToHglobal(IDirect3DSurface9* surface, int width, int height)
{
    AVFrame* tmp_frame = av_frame_alloc();

    int res = dxva2_convert_data(surface, tmp_frame, width, height);
    if (res != 0)
    {
        av_frame_free(&tmp_frame);
        return NULL;
    }

    auto img_convert_ctx = sws_getContext(tmp_frame->width, tmp_frame->height, (AVPixelFormat)tmp_frame->format,
        width, height, AV_PIX_FMT_BGR24, SWS_FAST_BILINEAR, NULL, NULL, NULL);

    const int stride = ((width * 3) + 3) & ~3;

    auto hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + stride * height);

    if (hMem == NULL) {
        av_frame_free(&tmp_frame);
        return NULL;
    }

    auto bmi = static_cast<BITMAPINFOHEADER*>(GlobalLock(hMem));
    if (!bmi) {
        GlobalFree(hMem);
        av_frame_free(&tmp_frame);
        return NULL;
    }

    memset(bmi, 0, sizeof(BITMAPINFOHEADER));
    bmi->biSize = sizeof(BITMAPINFOHEADER);
    bmi->biWidth = width;
    bmi->biHeight = -height;

    bmi->biPlanes = 1;
    bmi->biBitCount = 24; 
    bmi->biCompression = BI_RGB;

    const auto pData = static_cast<uint8_t*>(static_cast<void*>(bmi + 1));

    sws_scale(img_convert_ctx, tmp_frame->data, tmp_frame->linesize, 0, height, 
        &pData, &stride);

    GlobalUnlock(hMem);
    av_frame_free(&tmp_frame);

    return hMem;
}
