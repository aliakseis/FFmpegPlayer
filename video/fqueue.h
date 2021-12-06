#pragma once

#include <boost/thread/thread.hpp>
#include <deque>
#include <map>
#include <type_traits>

template<size_t MAX_QUEUE_SIZE, size_t MAX_FRAMES>
class FQueue
{
public:
    FQueue()  {}
    FQueue(const FQueue&) = delete;
    FQueue& operator=(const FQueue&) = delete;

    template<typename T>
    bool push(const AVPacket& packet, T abortFunc)
    {
        const auto pos = packet.pos;
        const auto dts = packet.dts;
        auto& prev = m_positions[packet.stream_index];
        if (pos != -1 && pos < prev.m_pos
            || !(pos != -1 && pos > prev.m_pos)
            && dts != AV_NOPTS_VALUE && dts < prev.m_dts)
        {
            return false;
        }

        bool wasEmpty;
        {
            boost::unique_lock<boost::mutex> locker(m_mutex);
            while (isPacketsQueueFull())
            {
                if (abortFunc())
                {
                    return false;
                }
                m_condVar.wait(locker);
            }
            wasEmpty = m_queue.empty();
            enqueue(packet);
            if (pos != -1)
                prev.m_pos = pos;
            if (dts != AV_NOPTS_VALUE)
                prev.m_dts = dts;
        }
        if (wasEmpty)
        {
            m_condVar.notify_all();
        }

        return true;
    }

    template<typename T = std::false_type>
    bool pop(AVPacket& packet, T abortFunc = T())
    {
        bool wasFull;
        {
            boost::unique_lock<boost::mutex> locker(m_mutex);

            while (m_queue.empty())
            {
                if (abortFunc())
                {
                    return false;
                }
                m_condVar.wait(locker);
            }

            wasFull = isPacketsQueueFull();
            packet = dequeue();
        }
        if (wasFull)
        {
            m_condVar.notify_all();
        }

        return true;
    }

    void clear()
    {
        for (AVPacket& packet : m_queue)
        {
            av_packet_unref(&packet);
        }
        m_packetsSize = 0;
        m_positions.clear();
        std::deque<AVPacket>().swap(m_queue);
    }

    bool empty()
    {
        boost::lock_guard<boost::mutex> locker(m_mutex);
        return m_queue.empty();
    }

    void notify()
    {
        m_condVar.notify_all();
    }

private:
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

    bool isPacketsQueueFull() const
    {
        return m_packetsSize > MAX_QUEUE_SIZE ||
            m_queue.size() > MAX_FRAMES;
    }

private:
    struct PositionData
    {
        int64_t	m_pos = -1;
        int64_t m_dts = AV_NOPTS_VALUE;
    };

    int64_t	m_packetsSize = 0;
    std::deque<AVPacket> m_queue;

    boost::mutex m_mutex;
    boost::condition_variable m_condVar;

    std::map<int, PositionData> m_positions;
};
