# Player

A feature-rich Windows video player written in C++ using **MFC**, **Direct3D**, **Direct2D**, **DXVA2**, and a modular decoder backend. The application focuses on smooth playback, hardware-accelerated rendering, subtitle handling, online media integration, and video processing.

Unlike a typical media player, this project contains a number of experimental components for frame processing, GPU effects, clipboard integration, URL detection, and AI-assisted image enhancement.

---

# Features

## Video Playback

* Hardware accelerated video rendering
* Direct3D 9 video presentation
* Optional Direct2D rendering path
* DXVA2 video processing support
* YUV420 and YUY2 rendering pipelines
* Automatic color space conversion
* Aspect ratio preserving scaling
* High refresh presentation (60 FPS rendering loop)
* Fullscreen playback
* Pause / Resume playback
* Timeline seeking
* Range selection support

---

## Hardware Acceleration

* DXVA2 video processing
* GPU color conversion
* GPU scaling
* Direct3D textures
* Video surfaces
* Software rasterizer fallback when GPU features are unavailable

---

## Subtitle Support

* External subtitle loading
* Subtitle synchronization
* Subtitle positioning
* Subtitle rendering during playback
* Automatic subtitle opening helpers

---

## Online Media

* Open videos directly from URLs
* Integrated URL dialog
* Automatic URL detection
* Asynchronous URL retrieval
* YouTube integration
* Browser clipboard URL support

---

## Image & Video Processing

The project includes significantly more than simple playback.

Built-in processing includes:

* Video filters
* GPU pixel shader effects
* I420 processing pipeline
* Frame transformation
* Frame extraction
* Frame conversion utilities
* Image upscaling support
* Clipboard frame export
* Frame-to-memory conversion
* Direct frame manipulation

---

## User Interface

* Traditional MFC SDI interface
* Dockable playback controls
* Timeline/range control
* Keyboard shortcuts
* Fullscreen toolbar
* Status bar
* Modern toolbar support
* Taskbar thumbnail toolbar integration
* High DPI icon loading

---

## Playback Controls

* Play
* Pause
* Resume
* Seek
* Timeline navigation
* Fullscreen mode
* Keyboard shortcuts
* Mouse interaction
* Time editing widgets

---

## Clipboard Integration

The player contains surprisingly extensive clipboard functionality.

Supported operations include:

* Read URLs from clipboard
* Read text from clipboard
* Export decoded frames
* Copy frame data through global memory handles

---

## Video Effects

Available infrastructure includes:

* Pixel shader based processing
* I420 GPU effects
* Color manipulation
* Frame transformations
* Video filter dialog
* Extensible effect architecture

---

## Performance

Designed with efficient rendering in mind.

Performance-oriented features include:

* GPU accelerated presentation
* Hardware video surfaces
* Direct memory access between decoder and renderer
* Buffered byte streams
* Minimal rendering copies where possible
* Asynchronous helper tasks

---

# Hidden Features

Several capabilities are not immediately visible from the UI but are implemented internally.

These include:

* YouTube playback infrastructure
* Automatic URL recognition
* Browser integration helpers
* Clipboard URL extraction
* Frame export pipeline
* Delegate/event system
* Modular decoder interface
* Optional Direct2D rendering backend
* Software rasterizer fallback
* GPU pixel shader framework
* Zlib compression utilities
* Boost.Log diagnostic logging
* Power management event handling
* Taskbar thumbnail toolbar support

---

# Architecture

The project is organized into relatively independent modules:

* Rendering
* Decoder abstraction
* Playback controls
* Subtitle handling
* Video filters
* Image processing
* Frame conversion
* URL handling
* Clipboard integration
* Utility helpers

This separation makes it practical to replace the decoder backend while preserving the rendering and user interface.

---

# Technologies

* C++
* MFC
* Direct3D 9
* Direct2D
* DXVA2
* Windows Media Foundation integration
* Boost.Log
* zlib
* HLSL Pixel Shaders
* Win32 API

---

# Interesting Technical Highlights

Compared to a typical desktop media player, this project contains several advanced engineering ideas:

* GPU-based video presentation
* Multiple rendering backends
* Hardware-accelerated color conversion
* Pixel shader processing pipeline
* Frame extraction utilities
* Image enhancement infrastructure
* Asynchronous URL processing
* External subtitle support
* YouTube media support
* Modular playback architecture suitable for experimentation with new decoder backends.

