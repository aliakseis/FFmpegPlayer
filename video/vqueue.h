#pragma once

// Video frame struct for RGB24 frame (used by displays)
struct VQueue
{
    enum { QUEUE_SIZE = 2 }; // enough for displaying one frame.

    VideoFrame m_frames[QUEUE_SIZE];
    int m_write_counter;
    int m_read_counter;
    int m_busy;

    VQueue() : m_write_counter(0),
               m_read_counter(0),
               m_busy(0)
    {
    }
    VQueue(const VQueue&) = delete;
    VQueue& operator=(const VQueue&) = delete;

    void clear()
    {
        for (auto& frame : m_frames)
        {
            frame.free();
        }

        // Reset readers
        m_write_counter = 0;
        m_read_counter = 0;
        m_busy = 0;
    }

    void setDisplayTime(double displayTime)
    {
        for (auto& frame : m_frames)
        {
            frame.m_displayTime = displayTime;
        }
    }
};
