#include "stdafx.h"
#include "AudioPlayerImpl.h"

#include <malloc.h>


namespace {

enum 
{
    BLOCK_SIZE = 4096,
    BLOCK_COUNT  = 4,
};

WAVEHDR* allocateBlocks(int size, int count)
{
    const size_t totalBufferSize = (sizeof(WAVEHDR) + size) * count;
    // allocate memory for the entire set in one step
    char* buffer = (char*) calloc(totalBufferSize, 1);

    // and set up the pointers to each bit
    WAVEHDR* blocks = (WAVEHDR*)buffer;
    buffer += sizeof(WAVEHDR) * count;
    for (int i = 0; i < count; ++i)
    {
        blocks[i].dwBufferLength = size;
        blocks[i].lpData = buffer;
        buffer += size;
    }
    return blocks;
}

void freeBlocks(WAVEHDR* blockArray)
{
    free(blockArray);
}

} // namespace

AudioPlayerImpl::AudioPlayerImpl()
    : m_callback(nullptr)
    , m_waveOutput(NULL)
    , m_waveBlocks(nullptr)
    , m_evtHasFreeBlocks(CreateEvent(NULL, FALSE, FALSE, NULL))
    , m_bytesPerSecond(0)
{
}


AudioPlayerImpl::~AudioPlayerImpl()
{
    CloseHandle(m_evtHasFreeBlocks);
}

void AudioPlayerImpl::InitializeThread()
{
    TRACE("Old audio thread priority = %d\n", GetThreadPriority(GetCurrentThread()));
    VERIFY(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL));
}

void AudioPlayerImpl::DeinitializeThread()
{
}

void AudioPlayerImpl::WaveOutReset()
{
    waveOutReset(m_waveOutput);
}

void AudioPlayerImpl::Close()
{
    if (m_waveOutput != NULL)
    {
        waveOutReset(m_waveOutput);
        waveOutClose(m_waveOutput);
    }

    if (m_waveBlocks)
    {
        // unprepare any blocks that are still prepared
        for (int i = 0; i < m_waveFreeBlockCount; ++i)
        {
            if (m_waveBlocks[i].dwFlags & WHDR_PREPARED)
            {
                waveOutUnprepareHeader(m_waveOutput, &m_waveBlocks[i], sizeof(WAVEHDR));
            }
        }

        freeBlocks(m_waveBlocks);
    }
}

bool AudioPlayerImpl::Open(int bytesPerSample, int channels, int* samplesPerSec)
{
    m_waveBlocks = allocateBlocks(BLOCK_SIZE, BLOCK_COUNT);
    m_waveFreeBlockCount = BLOCK_COUNT;
    m_waveCurrentBlock = 0;

    WAVEFORMATEX waveFormat = {};

    waveFormat.wBitsPerSample = bytesPerSample << 3;
    waveFormat.nSamplesPerSec = *samplesPerSec;
    waveFormat.nChannels = channels;

    waveFormat.cbSize = 0; // size of _extra_ info
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nBlockAlign = (waveFormat.wBitsPerSample >> 3) * waveFormat.nChannels;
    waveFormat.nAvgBytesPerSec = waveFormat.nBlockAlign * waveFormat.nSamplesPerSec;
    m_bytesPerSecond = waveFormat.nAvgBytesPerSec;

    TRACE("Bits per sample = %d\n", waveFormat.wBitsPerSample);
    TRACE("Samples per second = %d\n", waveFormat.nSamplesPerSec);
    TRACE("Channels = %d\n", waveFormat.nChannels);
    TRACE("Block align = %d\n", waveFormat.nBlockAlign);
    TRACE("Average bit rate = %d\n", waveFormat.nAvgBytesPerSec);

    if (waveOutOpen(
        &m_waveOutput,
        WAVE_MAPPER,
        &waveFormat,
        (DWORD_PTR)waveOutProc,
        (DWORD_PTR)this,
        CALLBACK_FUNCTION
        ) != MMSYSERR_NOERROR)
    {
        TRACE("Unable to open WAVE_MAPPER device.\n");
        return false;
    }

    return true;
}

void AudioPlayerImpl::Reset()
{
    m_waveOutput = NULL;
    m_waveBlocks = nullptr;
    m_waveFreeBlockCount = BLOCK_COUNT;
    ResetEvent(m_evtHasFreeBlocks);
    m_waveCurrentBlock = 0;
}

void AudioPlayerImpl::SetVolume(double volume)
{
    WORD vol = (WORD)(volume * 0xFFFF);
    // Left and right channels
    waveOutSetVolume(m_waveOutput, (DWORD)(vol << 16 | vol & 0xFFFF));
}

double AudioPlayerImpl::GetVolume() const
{
    const double point = 1. / 0xFFFF;
    DWORD volume = 0;
    waveOutGetVolume(m_waveOutput, &volume);
    return (volume & 0xFFFF) * point;
}

void AudioPlayerImpl::WaveOutPause()
{
    waveOutPause(m_waveOutput);
}

void AudioPlayerImpl::WaveOutRestart()
{
    waveOutRestart(m_waveOutput);
}

bool AudioPlayerImpl::WriteAudio(uint8_t* write_data, int64_t write_size)
{
    if (!m_waveOutput)
        return false;

    WAVEHDR* current = &m_waveBlocks[m_waveCurrentBlock];
    while (write_size > 0)
    {
        // fill block partially and wait for the next writeAudio call
        if (write_size < (int)(BLOCK_SIZE - current->dwUser))
        {
            memcpy(current->lpData + current->dwUser, write_data, (size_t)write_size);
            current->dwUser += (DWORD_PTR)write_size;
            break;
        }

        // fill block completely and play it
        const int remain = BLOCK_SIZE - current->dwUser;
        memcpy(current->lpData + current->dwUser, write_data, remain);

        write_size -= remain;
        write_data += remain;

        ASSERT(current->dwBufferLength == BLOCK_SIZE);
        if (current->dwFlags & WHDR_PREPARED || waveOutPrepareHeader(m_waveOutput, current, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
        {
            if (-1 == InterlockedDecrement(&m_waveFreeBlockCount))
            {
                WaitForSingleObject(m_evtHasFreeBlocks, INFINITE);
            }

            MMRESULT isOutWritten = waveOutWrite(m_waveOutput, current, sizeof(WAVEHDR));
            if (isOutWritten != MMSYSERR_NOERROR)
            {
                InterlockedIncrement(&m_waveFreeBlockCount);
            }
        }

        // point to the next block
        ++m_waveCurrentBlock;
        m_waveCurrentBlock %= BLOCK_COUNT;
        current = &m_waveBlocks[m_waveCurrentBlock];
        current->dwUser = 0;
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////////


void CALLBACK AudioPlayerImpl::waveOutProc(HWAVEOUT, UINT uMsg, DWORD dwInstance, DWORD, DWORD)
{
    // ignore calls that occur due to openining and closing the device.
    if (uMsg == WOM_DONE)
    {
        // pointer to free block counter
        AudioPlayerImpl* waveArgs = reinterpret_cast<AudioPlayerImpl*>(dwInstance);

        if (0 == InterlockedIncrement(&waveArgs->m_waveFreeBlockCount))
        {
            SetEvent(waveArgs->m_evtHasFreeBlocks);
        }

        // count audio pts
        const double frame_clock = (double)BLOCK_SIZE / waveArgs->m_bytesPerSecond;
        waveArgs->m_callback->AppendFrameClock(frame_clock);
    }
}
