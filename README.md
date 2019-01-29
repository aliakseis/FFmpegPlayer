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

YouTube view support using https://github.com/nficano/pytube.git can be turned on by uncommenting define YOUTUBE_EXPERIMENT in YouTuber.cpp. Python is also needed in this case:

```
vcpkg install python3

```

The matching Python version (currently 3.6) has to be installed and added to the PATH environment variable for the accessory DLLs to be accessible.

It is also possible that Boost::Python stuff will have to be enabled:
```
vcpkg install --featurepackages --recurse boost[python]

```

You may need to remove pytube stuff from your profile folder to for the player application to set up the latest version.

Tiny demo here: https://www.youtube.com/watch?v=dySA4yEGdEc
