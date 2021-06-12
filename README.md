# FFmpegPlayer

A simple FFmpeg based player. The player core is generic and made with multiplatformity in mind. UI / video / audio layer is MFC/Win32 specific. It turns out that there is no need to use multimedia libraries.

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
https://github.com/pytube/pytube/archive/master.zip = https://github.com/Ssuwani/pytube/archive/refs/heads/bug/video_info_url_404.zip
```

Sometimes we need to go deeper, visit pytube issues list and apply fixes, for example 
- https://github.com/get-pytube/pytube3/issues/81
- https://github.com/H4KKR/pytubeX/pull/5/commits

Take into account https://www.psiphon3.com if you encounter HTTP Error 429.

Just in case: "In fact in boost-python, the default behavior is that even when debug boost libraries are created, these libraries are linked to the release pythonX.dll/lib - by intention, according to the docs." https://github.com/pybind/pybind11/issues/1295

Tiny demos here: https://www.youtube.com/watch?v=dySA4yEGdEc https://www.youtube.com/watch?v=t5iW2ZsEzrA

Hint: hold Ctrl+Shift while submitting File Open dialog to choose a separate audio file. It works for the file opening from the Windows Explorer as well.

Please take into account specific Windows 10 behavior while opening Internet shortcuts: https://community.spiceworks.com/topic/1968971-opening-web-links-downloading-1-item-to-zcrksihu You can avoid this by dragging and dropping them.
