#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <initializer_list>
#include <functional>
#include <streambuf>


struct IDirect3DDevice9;
struct IDirect3DSurface9;

struct IFrameDecoder;

// Structure holding rendering data for a frame
struct FrameRenderingData
{
    uint8_t** image{};      // Pointer to frame image data
    const int* pitch{};     // Pointer to pitch (stride) values for each plane
    int width;              // Frame width
    int height;             // Frame height
    int aspectNum;          // Numerator of aspect ratio
    int aspectDen;          // Denominator of aspect ratio

    IDirect3DDevice9* d3d9device{};   // Direct3D device interface
    IDirect3DSurface9* surface{};     // Direct3D surface for rendering
};

// Interface for listening to frame updates
struct IFrameListener
{
    virtual ~IFrameListener() = default;
    virtual void updateFrame(IFrameDecoder* decoder, unsigned int generation) = 0; // Called to update frame data
    virtual void drawFrame(IFrameDecoder* decoder, unsigned int generation) = 0;  // Called to draw frame; decoder->finishedDisplayingFrame() must be called
    virtual void decoderClosing() = 0; // Called when decoder is closing
};

// Interface for decoder event notifications
struct FrameDecoderListener
{
    virtual ~FrameDecoderListener() = default;

    virtual void changedFramePosition(long long /*start*/, long long /*frame*/, long long /*total*/) {}
    virtual void decoderClosed(bool /*fileReleased*/) {}   // Notification of decoder closure
    virtual void fileLoaded(long long /*start*/, long long /*total*/) {} // Called when a file has been successfully loaded
    virtual void volumeChanged(double /*volume*/) {}  // Called when volume level changes

    virtual void onEndOfStream(int /*idx*/, bool /*error*/) {} // Called when the end of the stream is reached
    virtual void onQueueFull(int /*idx*/) {}  // Called when the frame queue is full

    virtual void playingFinished() {}  // Called when playback finishes
};

// Structure representing a rational number
struct RationalNumber
{
    int numerator;   // Numerator of fraction
    int denominator; // Denominator of fraction
};

// Equality operator for RationalNumber
inline bool operator==(const RationalNumber& left, const RationalNumber& right)
{
    return left.numerator == right.numerator && left.denominator == right.denominator;
}

// Interface for video frame decoding
struct IFrameDecoder
{
    // Supported pixel formats for frames
    enum FrameFormat {
        PIX_FMT_YUV420P,   ///< Planar YUV 4:2:0 format, 12 bits per pixel
        PIX_FMT_YUYV422,   ///< Packed YUV 4:2:2 format, 16 bits per pixel
        PIX_FMT_RGB24,     ///< Packed RGB format (8 bits per channel)
        PIX_FMT_BGR24,     ///< Packed BGR format (8 bits per channel)
    };

    // Possible modes after displaying a frame. Controls how the decoder handles
    // frame memory and display state once a frame is no longer needed.
    enum FinishedDisplayingMode {
        RELEASE_FRAME,       ///< Release memory associated with the frame only.
        ///< Use when frame data is not required anymore,
        ///< but no explicit "end of display" notification
        ///< is desired.

        FINALIZE_DISPLAY,    ///< Mark display of this frame as complete without
        ///< releasing its memory. Typically used if another
        ///< subsystem (e.g. GPU renderer) still needs access
        ///< to the frame buffers until it decides to release.

        RELEASE_AND_FINALIZE ///< (Default) Both release frame memory and finalize
                             ///< display. This is the safest option in most cases
                             ///< and should be used unless special handling is
                             ///< required.
    };

    // Function type for performing image conversion
    typedef std::function<void(
        uint8_t* /*input*/, int /*input stride*/, int /*input width*/, int /*input height*/,
        std::vector<uint8_t>& /*output*/, int& /*output width*/, int& /*output height*/
        )> ImageConversionFunc;

    virtual ~IFrameDecoder() = default;

    // Set frame format and whether Direct3D data should be allowed
    virtual void SetFrameFormat(FrameFormat format, bool allowDirect3dData) = 0;

    // Open video streams or URLs
    virtual bool openUrls(std::initializer_list<std::string> urls, const std::string& inputFormat = {}, bool useSAN = false) = 0;
    virtual bool openStream(std::unique_ptr<std::streambuf> stream) = 0;

    // Playback controls
    virtual void play(bool isPaused = false) = 0;
    virtual bool pauseResume() = 0;
    virtual bool nextFrame() = 0;
    virtual bool prevFrame() = 0;
    virtual void setVolume(double volume) = 0;

    virtual bool seekByPercent(double percent) = 0;
    virtual void videoReset() = 0;

    // Set event listeners
    virtual void setFrameListener(IFrameListener* listener) = 0;
    virtual void setDecoderListener(FrameDecoderListener* listener) = 0;

    // Retrieve rendering data
    virtual bool getFrameRenderingData(FrameRenderingData* data) = 0;

    virtual void doOnFinishedDisplayingFrame(unsigned int generation, FinishedDisplayingMode mode) = 0;

    // Notifies the decoder that a frame has finished displaying.
    //
    // Must be called exactly once for each frame that was passed to
    // IFrameListener::drawFrame(). This function may be called from
    // any thread, including UI/rendering threads, but the caller must
    // ensure correct synchronization.
    //
    // Parameters:
    //   generation - Frame generation number received with drawFrame()
    //   mode       - Frame release mode (defaults to RELEASE_AND_FINALIZE)
    //
    // Calling this function incorrectly (e.g., missing calls or wrong
    // mode) may result in memory leaks, excessive buffering, or playback stalls.
    void finishedDisplayingFrame(unsigned int generation, FinishedDisplayingMode mode = RELEASE_AND_FINALIZE)
    {
        doOnFinishedDisplayingFrame(generation, mode);
    }

    virtual void close() = 0;

    // Playback status queries
    virtual bool isPlaying() const = 0;
    virtual bool isPaused() const = 0;
    virtual double volume() const = 0;
    virtual double getDurationSecs(int64_t duration) const = 0;

    // Audio track management
    virtual int getNumAudioTracks() const = 0;
    virtual int getAudioTrack() const = 0;
    virtual void setAudioTrack(int idx) = 0;

    // Speed control
    virtual RationalNumber getSpeedRational() const = 0;
    virtual void setSpeedRational(const RationalNumber& speed) = 0;

    // Hardware acceleration control
    virtual bool getHwAccelerated() const = 0;
    virtual void setHwAccelerated(bool hwAccelerated) = 0;

    // Retrieve properties of the content
    virtual std::vector<std::string> getProperties() const = 0;

    // Check compatibility of video and audio streams
    virtual std::pair<bool, bool> isVideoAudioCompatible() const = 0;

    // Subtitle handling
    virtual std::vector<std::string> listSubtitles() const = 0;
    virtual bool getSubtitles(int idx, std::function<bool(double, double, const std::string&)> addIntervalCallback) = 0;

    // Set custom image conversion function
    virtual void setImageConversionFunc(ImageConversionFunc func) = 0;
};

struct IAudioPlayer;

// Function to retrieve a frame decoder with an associated audio player
std::unique_ptr<IFrameDecoder> GetFrameDecoder(std::unique_ptr<IAudioPlayer> audioPlayer);
