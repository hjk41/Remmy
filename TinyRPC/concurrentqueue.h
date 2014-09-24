#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <atomic>

namespace TinyRPC
{

    template<class T>
    class ConcurrentQueue
    {
    public:
        explicit ConcurrentQueue() : m_exitNow(false)
        {
        }

        void push(const T & e)
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_queue.push_back(e);
            m_cv.notify_one();
        }

        bool pop(T & rv)
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            while (m_queue.empty() && !m_exitNow)
                m_cv.wait(lk);
            if (m_exitNow)
                return false;
            ASSERT(!m_queue.empty(), "");
            rv = m_queue.front();
            m_queue.pop_front();
            return true;
        }

        std::list<T> popAll()
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            std::list<T> rv;
            rv.swap(m_queue);
            return rv;
        }

        size_t size()
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            return m_queue.size();
        }

        void signalForKill()
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_exitNow = true;
            m_cv.notify_all();
        }
    private:
        std::list<T> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_exitNow;
    };
}