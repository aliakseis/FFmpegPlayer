#pragma once

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

#ifdef _WIN32

typedef struct AVCodecContext AVCodecContext;
typedef struct AVFrame AVFrame;

typedef struct IDirect3DDevice9 IDirect3DDevice9;
typedef struct IDirect3DSurface9 IDirect3DSurface9;

int dxva2_init(AVCodecContext *s);
void dxva2_uninit(void* ist);
int dxva2_convert_data(IDirect3DSurface9* surface, AVFrame *tmp_frame, int width, int height);
int dxva2_retrieve_data(AVCodecContext *s, AVFrame *frame);

IDirect3DDevice9* get_device(AVCodecContext *s);

#endif // _WIN32
