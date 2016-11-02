#pragma once

#include <condition_variable>
#include <list>
#include <mutex>

namespace tinyrpc {

    template<class T>
    class ConcurrentQueue {
    public:
        explicit ConcurrentQueue() : exit_now_(false) {
        }

        void Push(const T & e) {
            std::lock_guard<std::mutex> lk(mutex_);
            queue_.push_back(e);
            cv_.notify_one();
        }

        bool Pop(T & rv) {
            std::unique_lock<std::mutex> lk(mutex_);
            while (queue_.empty() && !exit_now_)
                cv_.wait(lk);
            if (exit_now_)
                return false;
            ASSERT(!queue_.empty(), "");
            rv = queue_.front();
            queue_.pop_front();
            return true;
        }

        std::list<T> PopAll() {
            std::lock_guard<std::mutex> lk(mutex_);
            std::list<T> rv;
            rv.swap(queue_);
            return rv;
        }

        size_t Size() {
            std::lock_guard<std::mutex> lk(mutex_);
            return queue_.size();
        }

        void SignalForKill() {
            std::lock_guard<std::mutex> lk(mutex_);
            exit_now_ = true;
            cv_.notify_all();
        }
    private:
        std::list<T> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool exit_now_;
    };
}