# FFmpegPlayer

A simple FFmpeg based player. The player core is made with multiplatformity in mind. UI / video / audio layer is MFC/Win32 specific.
Note that D2D mode (the one that is turned on by uncommenting define USE_DIRECT2D_VIEW) has become unstable after related MFC changes. It demonstrated comparatively bad performance on my PC anyway, so it is here basically for demonstration purposes. 

[Semi transparent, click through full screen mode introduced.](https://bit.ly/2JLTbQn) It is invokable by holding ctrl+shift while pressing full screen button.

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

The matching Python version (currently 3.7) has to be installed and added to the PATH environment variable for the accessory DLLs to be accessible.

It is also possible that Boost::Python stuff will have to be enabled:
```
vcpkg install --featurepackages --recurse boost[python]

```

You may need to remove pytube stuff from your profile folder for the player application to set up the latest version, for example, by invoking remove_pytube.cmd.

You can also "patch" Python stuff by creating %LOCALAPPDATA%\git-subst.cfg mapping file that contains, for example,

```
https://github.com/nficano/pytube/archive/master.zip = https://github.com/hbmartin/pytube3/archive/master.zip
```

Sometimes we need to go deeper, visit pytube issues list and apply fixes, for example 
- https://github.com/nficano/pytube/issues/467#issuecomment-567560796
- https://github.com/nficano/pytube/pull/425
- https://github.com/nficano/pytube/pull/395
- https://github.com/nficano/pytube/issues/381

Just in case: "In fact in boost-python, the default behavior is that even when debug boost libraries are created, these libraries are linked to the release pythonX.dll/lib - by intention, according to the docs." https://github.com/pybind/pybind11/issues/1295

If you want YouTube subtitles, please note: https://github.com/microsoft/vcpkg/issues/6499 .
For now you can revert to Python 3.6 by running this in the vcpkg folder:

```
git checkout dfef7b111656e65a7e14078b6aaffa7f0a402308 -- ports/python3

```

Tiny demos here: https://www.youtube.com/watch?v=dySA4yEGdEc https://www.youtube.com/watch?v=t5iW2ZsEzrA
