#include "AudioPlayerWasapi.h"

#include <Audioclient.h>
#include <MMDeviceAPI.h>
#include <Avrt.h>

#include <string>
#include <atlstr.h>

namespace {

HMODULE GetAvrtHandle()
{
    static HMODULE avrtHandle = LoadLibrary(_T("Avrt.dll"));
    return avrtHandle;
}

}

// https://chromium.googlesource.com/chromium/src/media/+/master/audio/win/core_audio_util_win.cc
namespace CoreAudioUtil
{

    CComPtr<IMMDeviceEnumerator> CreateDeviceEnumeratorInternal(
        bool allow_reinitialize) {
        CComPtr<IMMDeviceEnumerator> device_enumerator;
        HRESULT hr = device_enumerator.CoCreateInstance(__uuidof(MMDeviceEnumerator),
            NULL, CLSCTX_INPROC_SERVER);
        if (hr == CO_E_NOTINITIALIZED && allow_reinitialize) {
            ATLTRACE("CoCreateInstance fails with CO_E_NOTINITIALIZED\n");
            // We have seen crashes which indicates that this method can in fact
            // fail with CO_E_NOTINITIALIZED in combination with certain 3rd party
            // modules. Calling CoInitializeEx is an attempt to resolve the reported
            // issues. See http://crbug.com/378465 for details.
            hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr)) {
                hr = device_enumerator.CoCreateInstance(__uuidof(MMDeviceEnumerator),
                    NULL, CLSCTX_INPROC_SERVER);
            }
        }
        return device_enumerator;
    }

    CComPtr<IMMDeviceEnumerator> CreateDeviceEnumerator() {
        //ASSERT(IsSupported());
        auto device_enumerator = CreateDeviceEnumeratorInternal(true);
        ATLASSERT(device_enumerator);
        return device_enumerator;
    }

    CComPtr<IMMDevice> CreateDefaultDevice(EDataFlow data_flow,
        ERole role) {
        //ASSERT(IsSupported());
        CComPtr<IMMDevice> endpoint_device;
        // Create the IMMDeviceEnumerator interface.
        auto device_enumerator = CreateDeviceEnumerator();
        if (!device_enumerator)
            return endpoint_device;
        // Retrieve the default audio endpoint for the specified data-flow
        // direction and role.
        HRESULT hr = device_enumerator->GetDefaultAudioEndpoint(
            data_flow, role, &endpoint_device);
        if (FAILED(hr)) {
            ATLTRACE("IMMDeviceEnumerator::GetDefaultAudioEndpoint: %x\n", hr);
            return endpoint_device;
        }

        // Verify that the audio endpoint device is active, i.e., that the audio
        // adapter that connects to the endpoint device is present and enabled.
        DWORD state = DEVICE_STATE_DISABLED;
        hr = endpoint_device->GetState(&state);
        if (SUCCEEDED(hr)) {
            if (!(state & DEVICE_STATE_ACTIVE)) {
                ATLTRACE("Selected endpoint device is not active\n");
                endpoint_device.Release();
            }
        }
        return endpoint_device;
    }

    CComPtr<IMMDevice> CreateDevice(
        const std::string& device_id) {
        //ASSERT(IsSupported());
        CComPtr<IMMDevice> endpoint_device;
        // Create the IMMDeviceEnumerator interface.
        auto device_enumerator = CreateDeviceEnumerator();
        if (!device_enumerator)
            return endpoint_device;
        // Retrieve an audio device specified by an endpoint device-identification
        // string.
        HRESULT hr = device_enumerator->GetDevice(CAtlStringW(device_id.c_str()), &endpoint_device);
        if (FAILED(hr)) {
            ATLTRACE("IMMDeviceEnumerator::GetDevice: %x\n", hr);
        }
        return endpoint_device;
    }

    CComPtr<IAudioClient> CreateClient(
        IMMDevice* audio_device) {
        //ASSERT(IsSupported());
        // Creates and activates an IAudioClient COM object given the selected
        // endpoint device.
        CComPtr<IAudioClient> audio_client;
        HRESULT hr = audio_device->Activate(__uuidof(IAudioClient),
            CLSCTX_INPROC_SERVER,
            NULL,
            (void**)&audio_client);
        if (FAILED(hr)) {
            ATLTRACE("IMMDevice::Activate: %x\n", hr);
        }

        return audio_client;
    }


    CComPtr<IAudioClient> CreateDefaultClient(
    EDataFlow data_flow, ERole role) {
        //ASSERT(IsSupported());
        auto default_device = CreateDefaultDevice(data_flow, role);
        if (!default_device)
            return {};
        return CreateClient(default_device);
    }


    CComPtr<IAudioClient> CreateClient(
        const std::string& device_id, EDataFlow data_flow, ERole role) {
        if (device_id.empty())
            return CreateDefaultClient(data_flow, role);
        auto device = CreateDevice(device_id);
        if (!device)
            return {};
        return CreateClient(device);
    }

    HRESULT GetSharedModeMixFormat(
        IAudioClient* client, WAVEFORMATPCMEX* format) {
        //ASSERT(IsSupported());
        CComHeapPtr<WAVEFORMATPCMEX> format_pcmex;
        HRESULT hr = client->GetMixFormat(
            reinterpret_cast<WAVEFORMATEX**>(&format_pcmex));
        if (FAILED(hr))
            return hr;
        size_t bytes = sizeof(WAVEFORMATEX) + format_pcmex->Format.cbSize;
        ATLASSERT(bytes == sizeof(WAVEFORMATPCMEX));
        memcpy(format, format_pcmex, bytes);
        return hr;
    }

    DWORD GetChannelConfig(const std::string& device_id,
        EDataFlow data_flow) {
        auto client = CreateClient(device_id, data_flow, eConsole);
        WAVEFORMATPCMEX format = { 0 };
        if (!client || FAILED(GetSharedModeMixFormat(client, &format)))
            return 0;
        return format.dwChannelMask;
    }


    HRESULT SharedModeInitialize(
        IAudioClient* client, const WAVEFORMATPCMEX* format, HANDLE event_handle,
        unsigned int* endpoint_buffer_size, const GUID* session_guid) {
        //ASSERT(IsSupported());
        // Use default flags (i.e, dont set AUDCLNT_STREAMFLAGS_NOPERSIST) to
        // ensure that the volume level and muting state for a rendering session
        // are persistent across system restarts. The volume level and muting
        // state for a capture session are never persistent.
        DWORD stream_flags = 0;
        // Enable event-driven streaming if a valid event handle is provided.
        // After the stream starts, the audio engine will signal the event handle
        // to notify the client each time a buffer becomes ready to process.
        // Event-driven buffering is supported for both rendering and capturing.
        // Both shared-mode and exclusive-mode streams can use event-driven buffering.
        bool use_event = (event_handle != NULL &&
            event_handle != INVALID_HANDLE_VALUE);
        if (use_event)
            stream_flags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        ATLTRACE("stream_flags: 0x%x\n", stream_flags);
        // Initialize the shared mode client for minimal delay.
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            stream_flags,
            100 * 10000,
            0,
            reinterpret_cast<const WAVEFORMATEX*>(format),
            session_guid);
        if (FAILED(hr)) {
            ATLTRACE("IAudioClient::Initialize: %x\n", hr);
            return hr;
        }
        if (use_event) {
            hr = client->SetEventHandle(event_handle);
            if (FAILED(hr)) {
                ATLTRACE("IAudioClient::SetEventHandle: %x\n", hr);
                return hr;
            }
        }
        UINT32 buffer_size_in_frames = 0;
        hr = client->GetBufferSize(&buffer_size_in_frames);
        if (FAILED(hr)) {
            ATLTRACE("IAudioClient::GetBufferSize: %x\n", hr);
            return hr;
        }
        *endpoint_buffer_size = buffer_size_in_frames;
        ATLTRACE("endpoint buffer size: %d\n", buffer_size_in_frames);

        return hr;
    }

    CComPtr<IAudioRenderClient> CreateRenderClient(
        IAudioClient* client) {
        //ASSERT(IsSupported());
        // Get access to the IAudioRenderClient interface. This interface
        // enables us to write output data to a rendering endpoint buffer.
        CComPtr<IAudioRenderClient> audio_render_client;
        HRESULT hr = client->GetService(__uuidof(IAudioRenderClient),
            (void**) &audio_render_client);
        if (FAILED(hr)) {
            ATLTRACE("IAudioClient::GetService: %x\n", hr);
            return CComPtr<IAudioRenderClient>();
        }
        return audio_render_client;
    }

} // namespace CoreAudioUtil

const GUID kCommunicationsSessionId = {
    0xbe39af4f, 0x87c, 0x423f, { 0x93, 0x3, 0x23, 0x4e, 0xc1, 0xe5, 0xb8, 0xee }
};

AudioPlayerWasapi::AudioPlayerWasapi()
    : m_callback(nullptr)
    , m_hAudioSamplesRenderEvent(CreateEvent(NULL, FALSE, FALSE, NULL))
    , m_BufferSize(0)
    , m_FrameSize(0)
    , m_samplesPerSec(0)
    , m_mmcssHandle(NULL)
    , m_coUnitialize(false)
{

}

AudioPlayerWasapi::~AudioPlayerWasapi()
{
    CloseHandle(m_hAudioSamplesRenderEvent);
}

bool AudioPlayerWasapi::Open(int bytesPerSample, int channels, int* samplesPerSec)
{
    const ERole device_role_ = eConsole;

    // Will be set to true if we ended up opening the default communications device.
    const bool communications_device = (device_role_ == eCommunications);

    // Create an IAudioClient interface for the default rendering IMMDevice.
    auto audio_client = CoreAudioUtil::CreateDefaultClient(eRender, device_role_);
    if (!audio_client)
        return false;

    WAVEFORMATPCMEX format_ = {};

    HRESULT hr = CoreAudioUtil::GetSharedModeMixFormat(audio_client, &format_);
    if (FAILED(hr))
        return false;

    if (format_.Format.nSamplesPerSec != 0)
    {
        *samplesPerSec = format_.Format.nSamplesPerSec;
    }

    // Begin with the WAVEFORMATEX structure that specifies the basic format.
    WAVEFORMATEX* format = &format_.Format;
    format->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format->nChannels = channels;
    format->nSamplesPerSec = *samplesPerSec;
    format->wBitsPerSample = bytesPerSample << 3;
    format->nBlockAlign = (format->wBitsPerSample / 8) * format->nChannels;
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    format->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    m_FrameSize = format->nBlockAlign;

    m_samplesPerSec = *samplesPerSec;

    // Add the parts which are unique to WAVE_FORMAT_EXTENSIBLE.
    format_.Samples.wValidBitsPerSample = bytesPerSample << 3; //params.bits_per_sample();
    //format_.dwChannelMask = CoreAudioUtil::GetChannelConfig(std::string(), eRender);
    format_.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    // Initialize the audio stream between the client and the device in shared
    // mode and using event-driven buffer handling.
    hr = CoreAudioUtil::SharedModeInitialize(
        audio_client, &format_, m_hAudioSamplesRenderEvent,
        &m_BufferSize,
        communications_device ? &kCommunicationsSessionId : NULL);
    if (FAILED(hr))
        return false;

    // Create an IAudioRenderClient client for an initialized IAudioClient.
    // The IAudioRenderClient interface enables us to write output data to
    // a rendering endpoint buffer.
    auto audio_render_client = CoreAudioUtil::CreateRenderClient(audio_client);
    if (!audio_render_client)
        return false;
    // Store valid COM interfaces.
    m_AudioClient = audio_client;
    m_RenderClient = audio_render_client;

    hr = m_AudioClient->GetService(__uuidof(ISimpleAudioVolume), (void**)&m_SimpleAudioVolume);

    m_AudioClient->Start();

    return true;
}

bool AudioPlayerWasapi::WriteAudio(uint8_t* write_data, int64_t write_size)
{
    while (write_size > 0)
    {
        //  We need to provide the next buffer of samples to the audio renderer.
        //  We want to find out how much of the buffer *isn't* available (is padding).
        UINT32 padding;
        HRESULT hr = m_AudioClient->GetCurrentPadding(&padding);
        if (FAILED(hr))
        {
            ATLTRACE("Unable to get padding: %x\n", hr);
            return false;
        }

        UINT32 framesAvailable = m_BufferSize - padding;

        if (framesAvailable != 0)
        {
            const auto remain = min(write_size, framesAvailable * m_FrameSize);

            UINT32 framesToWrite = UINT32(remain / m_FrameSize);
            BYTE *pData;
            hr = m_RenderClient->GetBuffer(framesToWrite, &pData);
            if (SUCCEEDED(hr))
            {
                //  Copy data from the render buffer to the output buffer and bump our render pointer.
                memcpy(pData, write_data, framesToWrite * m_FrameSize);
                hr = m_RenderClient->ReleaseBuffer(framesToWrite, 0);
                if (!SUCCEEDED(hr))
                {
                    ATLTRACE("Unable to release buffer: %x\n", hr);
                }

                // count audio pts
                const double frame_clock = (double)framesToWrite / m_samplesPerSec;
                m_callback->AppendFrameClock(frame_clock);
            }
            else
            {
                ATLTRACE("Unable to get buffer: %x\n", hr);
            }
            write_size -= remain;
            write_data += remain;
        }
        if (write_size > 0)
        {
            WaitForSingleObject(m_hAudioSamplesRenderEvent, INFINITE);
        }

    }

    return true;
}

#define AVRT_FUNC_PTR(name) \
    static decltype(name)* name##Ptr = (decltype(name)*)GetProcAddress(avrtHandle, #name)

void AudioPlayerWasapi::InitializeThread()
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    m_coUnitialize = (S_OK == hr);
    if (FAILED(hr))
    {
        ATLTRACE("Unable to initialize COM in render thread: %x\n", hr);
    }

    ATLTRACE("Old audio thread priority = %d\n", GetThreadPriority(GetCurrentThread()));
    ATLVERIFY(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL));

    if (auto avrtHandle = GetAvrtHandle())
    {
        DWORD mmcssTaskIndex = 0;
        AVRT_FUNC_PTR(AvSetMmThreadCharacteristicsW);
        if (AvSetMmThreadCharacteristicsWPtr)
        {
            m_mmcssHandle = AvSetMmThreadCharacteristicsWPtr(L"Pro Audio", &mmcssTaskIndex);
            if (m_mmcssHandle == NULL)
            {
                ATLTRACE("Unable to enable MMCSS on render thread: %d\n", GetLastError());
            }
            else
            {
                AVRT_FUNC_PTR(AvSetMmThreadPriority);
                if (AvSetMmThreadPriorityPtr)
                    ATLVERIFY(AvSetMmThreadPriorityPtr(m_mmcssHandle, AVRT_PRIORITY_CRITICAL));
            }
        }
    }
}

void AudioPlayerWasapi::DeinitializeThread()
{
    if (m_mmcssHandle)
    {
        if (auto avrtHandle = GetAvrtHandle())
        {
            AVRT_FUNC_PTR(AvRevertMmThreadCharacteristics);
            if (AvRevertMmThreadCharacteristicsPtr)
            {
                ATLVERIFY(AvRevertMmThreadCharacteristicsPtr(m_mmcssHandle));
                m_mmcssHandle = NULL;
            }
        }
    }

    if (m_coUnitialize)
    {
        CoUninitialize();
        m_coUnitialize = false;
    }
}

#undef AVRT_FUNC_PTR

void AudioPlayerWasapi::WaveOutReset()
{
    if (m_AudioClient)
    {
        m_AudioClient->Reset();
    }
}

void AudioPlayerWasapi::Close()
{
    m_SimpleAudioVolume.Release();
    m_RenderClient.Release();
    m_AudioClient.Release();
}

void AudioPlayerWasapi::Reset()
{
    if (m_AudioClient)
    {
        m_AudioClient->Reset();
    }
}

void AudioPlayerWasapi::SetVolume(double volume)
{
    if (m_SimpleAudioVolume)
    {
        m_SimpleAudioVolume->SetMasterVolume((float)volume, NULL);
    }
}

double AudioPlayerWasapi::GetVolume() const
{
    float result = 1.;
    if (m_SimpleAudioVolume)
    {
        m_SimpleAudioVolume->GetMasterVolume(&result);
    }
    return result;
}

void AudioPlayerWasapi::WaveOutPause()
{
    m_AudioClient->Stop();
}

void AudioPlayerWasapi::WaveOutRestart()
{
    m_AudioClient->Start();
}
