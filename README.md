# FFmpegPlayer

A simple FFmpeg based player. The player core is generic and made with multiplatformity in mind. UI / video / audio layer is MFC/Win32 specific. It turns out that there is no need to use multimedia libraries. There is also a Qt based demo example included.

[Semi transparent, click through full screen mode introduced.](https://bit.ly/2JLTbQn) It is invokable by holding ctrl+shift while pressing full screen button.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

### Prerequisites


- Visual Studio 2017 or higher.
- Intel SDK For OpenCL Applications installation is required for the super resolution functionality intergated.

### Installing

Be sure to download git submodules.

To get a development env running:

Install vcpkg from https://github.com/Microsoft/vcpkg.

```
.\vcpkg integrate install
```

Install Boost, FFmpeg, OpenCV etc... :

```
vcpkg install boost
vcpkg install ffmpeg[ffmpeg,x264,nonfree,gpl,vpx,webp,zlib]
...

```

YouTube view support using https://github.com/pytube/pytube.git is turned on by default. It can be turned off by commenting define YOUTUBE_EXPERIMENT in YouTuber.cpp. 
Python is also needed otherwise:

```
vcpkg install python3

```

The matching Python version has to be installed and added to the PATH environment variable for the accessory DLLs to be accessible.

It is also possible that Boost::Python stuff will have to be enabled:
```
vcpkg install --featurepackages --recurse boost[python]

```

You may need to remove pytube stuff from your profile folder for the player application to set up the latest version, for example, by invoking remove_pytube.cmd.

You can also "patch" Python stuff by creating %LOCALAPPDATA%\git-subst.cfg mapping file that contains, for example,

```
https://github.com/pytube/pytube/archive/master.zip = https://github.com/sadeghastaneh/pytube/archive/refs/heads/patch-1.zip
```

Sometimes it is needed to visit pytube issues list and apply fixes, for example 
- https://github.com/pytube/pytube/issues/1326

Take into account https://www.psiphon3.com if you encounter HTTP Error 429.

Just in case: "In fact in boost-python, the default behavior is that even when debug boost libraries are created, these libraries are linked to the release pythonX.dll/lib - by intention, according to the docs." https://github.com/pybind/pybind11/issues/1295

Tiny demos here: https://www.youtube.com/watch?v=dySA4yEGdEc https://www.youtube.com/watch?v=t5iW2ZsEzrA

Tip: hold Ctrl+Shift while submitting File Open dialog to choose a separate audio file. It works for the file opening from the Windows Explorer as well.

Please take into account specific Windows 10 behavior while opening Internet shortcuts: https://community.spiceworks.com/topic/1968971-opening-web-links-downloading-1-item-to-zcrksihu You can avoid this by dragging and dropping them.

### Bonus tip

Playing YouTube videos in browsers may result in poor performance on slow hardware. Assign a keyboard shortcut to the FFmpeg player by editing its shortcut. Hover your mouse over the YouTube link in Firefox and bring up the shortcut. A player pop-up window will appear, starting the video playback. The same can be achieved in Chrome with some tweaking.

![redline](https://user-images.githubusercontent.com/11851670/184552270-73cb8ba4-31f7-47f2-9f50-2b4ceae601e7.gif)
