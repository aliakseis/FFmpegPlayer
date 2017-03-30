#pragma once

#include <assert.h>

// Video frame struct for RGB24 frame (used by displays)
class VQueue
{
public:
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

    bool canPush() const { return m_busy < QUEUE_SIZE; }
    VideoFrame& back() { return m_frames[m_write_counter]; }
    void pushBack()
    {
        m_write_counter = (m_write_counter + 1) % QUEUE_SIZE;
        ++m_busy;
        assert(m_busy <= VQueue::QUEUE_SIZE);
    }

    bool canPop() const { return m_busy > 0; }
    VideoFrame& front() { return m_frames[m_read_counter]; }
    void popFront()
    {
        --m_busy;
        assert(m_busy >= 0);
        m_read_counter = (m_read_counter + 1) % QUEUE_SIZE;
    }

private:
    enum { QUEUE_SIZE = 2 }; // enough for displaying one frame.

    VideoFrame m_frames[QUEUE_SIZE];
    int m_write_counter;
    int m_read_counter;
    int m_busy;
};
