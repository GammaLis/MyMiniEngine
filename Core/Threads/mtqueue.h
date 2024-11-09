#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

// Ref:[C++11] Multi Threads
// https://github.com/parallel101/course/blob/master/slides/thread/mtqueue.hpp

namespace Timo
{
    template <typename T, typename Queue = std::deque<T>>
    class MtQueue
    {
        Queue m_Queue;
        std::mutex m_Mutex;
        std::condition_variable m_CV_empty;
        std::condition_variable m_CV_full;
        std::size_t m_Limit{};
        
    public:
        constexpr static size_t s_DefaultLimit = std::numeric_limits<size_t>::max(); 
        
        // Default
        MtQueue() : m_Limit(s_DefaultLimit) { }

        explicit MtQueue(std::size_t limit) : m_Limit(limit) { }

        std::size_t Size() const 
        {
            std::unique_lock lock(m_Mutex);
            return m_Queue.size();
        }

        void Push(T value)
        {
            std::unique_lock lock(m_Mutex);
            while (m_Queue.size() >= m_Limit)
                m_CV_full.wait(lock);
            m_Queue.push_back(value);
            m_CV_empty.notify_one();
        }

        bool TryPush(T value)
        {
            std::unique_lock lock(m_Mutex);
            if (m_Queue.size() >= m_Limit)
                return false;
            m_Queue.push_back(value);
            m_CV_empty.notify_one();
            return true;
        }

        bool TryPushFor(T value, std::chrono::steady_clock::duration timeout)
        {
            std::unique_lock lock(m_Mutex);
            if (!m_CV_full.wait_for(lock, timeout, [this]{ m_Queue.size() < m_Limit; }) )
                return false;
            m_Queue.push_back(value);
            m_CV_empty.notify_one();
            return true;
        }

        bool TryPushUntil(T value, std::chrono::steady_clock::time_point timepoint)
        {
            std::unique_lock lock(m_Mutex);
            if (m_CV_full.wait_until(lock, timepoint, [this]{ m_Queue.size() < m_Limit; }) )
                return false;
            m_Queue.push_back(value);
            m_CV_empty.notify_one();
            return true;            
        }

        T Pop()
        {
            std::unique_lock lock(m_Mutex);
            while (m_Queue.empty())
                m_CV_empty.wait(lock);
            T value = std::move(m_Queue.front());
            m_Queue.pop_front();
            m_CV_full.notify_one();
            return value;
        }

        std::optional<T> TryPop()
        {
            std::unique_lock lock(m_Mutex);
            if (m_Queue.empty())
                return std::nullopt;
            T value = std::move(m_Queue.front());
            m_Queue.pop_front();
            m_CV_full.notify_one();
            return  value;
        }

        std::optional<T> TryPopFor(std::chrono::steady_clock::duration timeout)
        {
            std::unique_lock lock(m_Mutex);
            if ( !m_CV_empty.wait_for(lock, timeout, [this]{ return !m_Queue.empty(); }) )
                return std::nullopt;
            T value = std::move(m_Queue.front());
            m_Queue.pop_front();
            m_CV_full.notify_one();
            return  value;
        }

        std::optional<T> TryPopUntil(std::chrono::steady_clock::time_point timepoint)
        {
            std::unique_lock lock(m_Mutex);
            if ( !m_CV_empty.wait_until(lock, timepoint, [this]{ return !m_Queue.empty(); }) )
                return std::nullopt;
            T value = std::move(m_Queue.front());
            m_Queue.pop_front();
            m_CV_full.notify_one();
            return  value;
        }
    };
    
}
