#pragma once

#include <deque>

class FQueue
{
public:
    FQueue() : m_packetsSize(0) {}

    AVPacket dequeue()
    {
        assert(!m_queue.empty());
        AVPacket packet = m_queue.front();
        m_queue.pop_front();
        m_packetsSize -= packet.size;
        assert(m_packetsSize >= 0);
        return packet;
    }

    void enqueue(const AVPacket& packet)
    {
        m_packetsSize += packet.size;
        assert(m_packetsSize >= 0);
        m_queue.push_back(packet);
    }

    int size() const
    {
        return m_queue.size();
    }

    bool empty() const
    {
        return m_queue.empty();
    }

    int64_t	packetsSize() const
    {
        return m_packetsSize;
    }

    void clear()
    {
        for (AVPacket& packet : m_queue)
        {
            av_packet_unref(&packet);
        }
        m_packetsSize = 0;
        std::deque<AVPacket>().swap(m_queue);
    }

private:
    int64_t	m_packetsSize;
    std::deque<AVPacket> m_queue;
};
