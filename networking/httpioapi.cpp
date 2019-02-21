/* ioapi.c -- IO base function header for compress/uncompress .zip
   files using zlib + zip or unzip API

   Version 1.01e, February 12th, 2005

   Copyright (C) 1998-2005 Gilles Vollant

   Modified to integrate with WinHttpRequest.
*/

#include <cassert>

#include "zlib.h"
#include "ioapi.h"

#include "httprequest_h.h"

#include <atlcomcli.h>


voidpf ZCALLBACK qiodevice_open_file_func(
    voidpf opaque,
    voidpf filename,
    int mode)
{
    VARIANT varFalse{ VT_BOOL };
    VARIANT varEmpty{ VT_ERROR };

    CComVariant varStream;

    CComPtr<IWinHttpRequest> pIWinHttpRequest;

    if (SUCCEEDED(pIWinHttpRequest.CoCreateInstance(L"WinHttp.WinHttpRequest.5.1", nullptr, CLSCTX_INPROC_SERVER))
        && SUCCEEDED(pIWinHttpRequest->Open(CComBSTR(L"GET"), CComBSTR(static_cast<const char*>(filename)), varFalse))
        && SUCCEEDED(pIWinHttpRequest->Send(varEmpty))
        && SUCCEEDED(pIWinHttpRequest->get_ResponseStream(&varStream)))
    {
        if (CComQIPtr<IStream> stream = V_UNKNOWN(&varStream))
        {
            return stream.Detach();
        }
    }

    return nullptr;
}


uLong ZCALLBACK qiodevice_read_file_func(
    voidpf, // opaque 
    voidpf pf,
    void* buf,
    uLong size)
{
    auto stream = static_cast<IStream*>(pf);
    uLong ret = 0;
    HRESULT hr = stream->Read(buf, size, &ret);
    return ret;
}


uLong ZCALLBACK qiodevice_write_file_func(
    voidpf, // opaque
    voidpf pf,
    const void* buf,
    uLong size)
{
    auto stream = static_cast<IStream*>(pf);
    uLong ret = 0;
    stream->Write(buf, size, &ret);
    return ret;
}

uLong ZCALLBACK qiodevice_tell_file_func(
    voidpf, // opaque
    voidpf pf)
{
    auto stream = static_cast<IStream*>(pf);
    const LARGE_INTEGER move{};
    ULARGE_INTEGER libNewPosition{};
    stream->Seek(move, STREAM_SEEK_CUR, &libNewPosition);
    return libNewPosition.LowPart;
}

int ZCALLBACK qiodevice_seek_file_func(
    voidpf, // opaque
    voidpf pf,
    uLong offset,
    int origin)
{
    STREAM_SEEK dwOrigin;
    LARGE_INTEGER move;
    switch (origin)
    {
    case ZLIB_FILEFUNC_SEEK_CUR:
        dwOrigin = STREAM_SEEK_CUR;
        move.QuadPart = (long)offset;
        break;
    case ZLIB_FILEFUNC_SEEK_END:
        dwOrigin = STREAM_SEEK_END;
        move.QuadPart = (long)offset;
        break;
    case ZLIB_FILEFUNC_SEEK_SET:
        dwOrigin = STREAM_SEEK_SET;
        move.QuadPart = offset;
        break;
    default: return -1;
    }
    auto stream = static_cast<IStream*>(pf);
    ULARGE_INTEGER libNewPosition{};
    int ret = FAILED(stream->Seek(move, dwOrigin, &libNewPosition));
    return ret;
}

int ZCALLBACK qiodevice_close_file_func(
    voidpf, // opaque
    voidpf pf)
{
    auto stream = static_cast<IStream*>(pf);
    auto cnt = stream->Release();
    assert(cnt == 0);
    return 0;
}

int ZCALLBACK qiodevice_error_file_func(
    voidpf, // opaque
    voidpf //stream
)
{
    return 0;
}

void fill_qiodevice_filefunc(
    zlib_filefunc_def* pzlib_filefunc_def)
{
    pzlib_filefunc_def->zopen_file = qiodevice_open_file_func;
    pzlib_filefunc_def->zread_file = qiodevice_read_file_func;
    pzlib_filefunc_def->zwrite_file = qiodevice_write_file_func;
    pzlib_filefunc_def->ztell_file = qiodevice_tell_file_func;
    pzlib_filefunc_def->zseek_file = qiodevice_seek_file_func;
    pzlib_filefunc_def->zclose_file = qiodevice_close_file_func;
    pzlib_filefunc_def->zerror_file = qiodevice_error_file_func;
    pzlib_filefunc_def->opaque = nullptr;
}
