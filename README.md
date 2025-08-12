# ℉ℲmpegPlayer

A simple FFmpeg based player. The player core is generic and made with multiplatformity in mind. UI / video / audio layer is MFC/Win32 specific. It turns out that there is no need to use multimedia libraries. There is also a Qt based demo example included. It offers: 
- Basic Playback Controls: Play/Pause, Stop.
- Next/Previous Frame: Step through the video one frame at a time during pause.
- Speed Change: Increase or decrease the playback speed without altering the pitch of the audio.
- Separate Video and Audio Inputs: Ability to load and play video and audio from separate sources.
- Audio Track Selection: Choose between different audio tracks if available.
- Fragment Selection for Export: Mark in and out points to select a part of the video for exporting.
- Repeated Playing: Loop the entire video/playlist or selected fragment continuously.
- Subtitles: Load and display subtitle files in various formats.
- Super Resolution: Enhance the resolution of the video using upscaling techniques.
- Codec Support: Compatibility with a wide range of video and audio codecs.
- Streaming Support: Ability to stream video from online sources.

[Semi transparent, click through full screen mode introduced.](https://bit.ly/2JLTbQn) It is invokable by holding ctrl+shift while pressing full screen button.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

### Prerequisites


- Visual Studio 2017 or higher.
- vcpkg.

### Installing

Be sure to download git submodules.

To get a development env running:

Install vcpkg from https://github.com/Microsoft/vcpkg.
```
.\vcpkg integrate install
```

Install Boost, FFmpeg, OpenCV etc. Details can be found in .github/workflows/msbuild.yml.

Create ./Directory.Build.props file in the project folder. It contents depend on you environment, for example:
```
<Project>
  <PropertyGroup>
    <WindowsTargetPlatformVersion>10.0.22621.0</WindowsTargetPlatformVersion>
    <TargetFrameworkVersion>v4.6.2</TargetFrameworkVersion>
    <PlatformToolset>v143</PlatformToolset>
  </PropertyGroup>
</Project>
```

YouTube view support is turned on by default. It can be turned off by commenting define YOUTUBE_EXPERIMENT in YouTuber.cpp. A special mode has been implemented: Google can use SAN certificates that include multiple domains and subdomains. This allows one certificate to protect multiple services, such as youtube.com, www.youtube.com, and other subdomains. If you have one certificate with multiple SANs, and all domains are served by one certificate, you can do without SNI by using the IP address in the URL and the Host header to identify the content.

The matching Python version has to be installed for the accessory DLLs to be accessible, except for embedded Python coming with installation. In any case Pytubefix now requires Node.js installed.

You may need to remove pytube stuff from your profile folder for the player application to set up the latest version, for example, by invoking remove_pytube.cmd.

You can also "patch" Python stuff by creating %LOCALAPPDATA%\git-subst.cfg mapping file.

Just in case: "In fact in boost-python, the default behavior is that even when debug boost libraries are created, these libraries are linked to the release pythonX.dll/lib - by intention, according to the docs." https://github.com/pybind/pybind11/issues/1295

Tiny demos here: https://www.youtube.com/watch?v=dySA4yEGdEc https://www.youtube.com/watch?v=t5iW2ZsEzrA

Tip: hold Ctrl+Shift while submitting File Open dialog to choose a separate audio file. It works for the file opening from the Windows Explorer as well.

Please take into account specific Windows 10 behavior while opening Internet shortcuts: https://community.spiceworks.com/topic/1968971-opening-web-links-downloading-1-item-to-zcrksihu You can avoid this by dragging and dropping them.

Note that the FFmpeg patch speeds up HEVC decoding without GPU support by ~10%:

![image](https://user-images.githubusercontent.com/11851670/171165625-3a111046-672c-4a75-8184-c91fde994e00.png)


### 🎬 Video Conversion Script Generation
This feature generates and runs a batch script that converts selected video files into a format compatible with basic players. The script adapts dynamically based on playback settings and optional media inputs.

#### 🔧 Controlled via Menu Options:
File → Convert Videos into Compatible Format Triggers the generation and running of a conversion script using FFmpeg. The script includes commands to re-encode or copy video, audio, and subtitle streams based on compatibility and user preferences.

File → Autoplay When enabled, the script processes a sequence of video files for automatic conversion. If combined with Looping, the entire sequence is included. If Looping is disabled, only the current and following files are processed.

File → Looping When Autoplay is disabled, this option includes both the current file and its predecessors in the conversion script. When Autoplay is enabled, it loops through the entire detected sequence of files.

Audio / Video → Open Audio File... Allows users to specify a separate audio file to be merged with the video during conversion. If provided, the script maps video from the original files and audio from the separate files.

Audio / Video → Open Subtitles File... Enables users to attach external subtitle files. These are converted to UTF-8 encoding using ToUTF8.exe and saved alongside the converted videos.

#### 🛠️ Conversion Details:
Uses FFmpeg for media processing.

Selects codecs based on compatibility:

libx264 for video re-encoding if needed

aac for audio if separate or incompatible

Copies subtitle streams when available

Outputs converted files to the specified folder, preserving original filenames.

### Bonus tip

Playing YouTube videos in browsers may result in poor performance on slow hardware. Assign a keyboard shortcut to the FFmpeg player by editing its shortcut. Hover your mouse over the YouTube link in Firefox and bring up the shortcut. A player pop-up window will appear, starting the video playback. The same can be achieved in Chrome with some tweaking. [Start Chrome with this flag: --force-renderer-accessibility](https://www.chromium.org/developers/design-documents/accessibility/) and / or [set up IAccessible2 COM proxy stub DLL](https://github.com/aliakseis/IAccessible2Proxy).

![redline](https://user-images.githubusercontent.com/11851670/184552270-73cb8ba4-31f7-47f2-9f50-2b4ceae601e7.gif)
