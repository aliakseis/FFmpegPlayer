# FFmpegPlayer

A simple FFmpeg based player. The player core is made with multiplatformity in mind. UI / video / audio layer is MFC/Win32 specific.
Note that D2D mode (the one that is turned on by uncommenting define USE_DIRECT2D_VIEW) has become unstable after related MFC changes. It demonstrated comparatively bad performance on my PC anyway, so it is here basically for demonstration purposes. 

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

### Prerequisites


```
Visual Studio 2015 or 2017
```

### Installing

To get a development env running:

Install vcpkg from https://github.com/Microsoft/vcpkg.

```
.\vcpkg integrate install
```

Install Boost, FFmpeg:

```
vcpkg install boost boost:x86-windows
vcpkg install ffmpeg ffmpeg:x86-windows

```

If libbz2.dll is missing from the player debug directory, then vcpkg/installed/x86-windows/debug/bin/libbz2d.dll file is to be copied into that. 
