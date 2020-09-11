#pragma once

#include <condition_variable>
#include <limits>
#include <list>
#include <mutex>

#include "logging.h"

namespace simple_rpc {

    template<class T>
    class ConcurrentQueue {
    public:
        explicit ConcurrentQueue(size_t max_elements = std::numeric_limits<size_t>::max()) 
            : exit_now_(false),
              max_elements_(max_elements) {
        }

        bool Push(T&& e) {
            return PushImpl(e);
        }

        bool Push(const T& e) {
            return PushImpl(e);
        }

        bool Pop(T & rv) {
            std::unique_lock<std::mutex> lk(mutex_);
            while (queue_.empty() && !exit_now_)
                cv_.wait(lk);
            if (queue_.empty())
                return false;
            TINY_ASSERT(!queue_.empty(), "");
            rv = queue_.front();
            queue_.pop_front();
            cv_.notify_all();
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
        template<typename TT>
        bool PushImpl(TT&& e) {
            std::unique_lock<std::mutex> lk(mutex_);
            while (queue_.size() >= max_elements_ && !exit_now_)
                cv_.wait(lk);
            if (queue_.size() >= max_elements_)
                return false;
            queue_.emplace_back(e);
            cv_.notify_one();
            return true;
        }

        size_t max_elements_;
        std::list<T> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool exit_now_;
    };
}