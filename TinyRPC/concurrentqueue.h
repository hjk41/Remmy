#pragma once

#include <condition_variable>
#include <list>
#include <mutex>

namespace TinyRPC
{

    template<class T>
    class ConcurrentQueue
    {
    public:
        explicit ConcurrentQueue()
        {
        }

        void push(const T & e)
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_queue.push_back(e);
            m_cv.notify_one();
        }

        T pop()
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            while (m_queue.empty())
                m_cv.wait(lk);
            assert(!m_queue.empty());
            T rv = m_queue.front();
            m_queue.pop_front();
            return rv;
        }

        std::list<T> popAll()
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            std::list<T> rv;
            rv.swap(m_queue);
            return rv;
        }

    private:
        std::list<T> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };
}