#pragma once

struct IDirect3DSurface9;

HGLOBAL FrameToHglobal(IDirect3DSurface9* surface, int width, int height, int allocatedHeight);
