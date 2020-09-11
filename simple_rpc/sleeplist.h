#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <stdint.h>
#include "logging.h"
#include "datatypes.h"

namespace simple_rpc {
    typedef std::lock_guard<std::mutex> LockGuard;

    template<class Response>
    class SleepingList {
        struct ResponseSignaled {
            ResponseSignaled()
            : response(nullptr),
            received(false),
            server_failure(false){}

            Response * response;
            bool received;
            bool server_failure;
            std::condition_variable cv;
        };
    public:
        SleepingList(){}

        void AddEvent(int64_t event, Response * r) {
            LockGuard l(lock_);
            ResponseSignaled *& rs = event_map_[event];
            TINY_ASSERT(rs == nullptr, "event already registered");
            rs = new ResponseSignaled();
            rs->response = r;
        }

        void RemoveEvent(int64_t event) {
            LockGuard l(lock_);
            RemoveEventLocked(event);
        }

        /// <summary>
        /// wait until the response has arrived or timeout has reached.
        /// </summary>
        /// <param name="event">The sequence id of the request.</param>
        /// <param name="timeout">The timeout in milliseconds, default 0 indicats infinity.</param>
        /// <returns>error code</returns>
        TinyErrorCode WaitForResponse(int64_t event, uint64_t timeout = 0) {
            std::unique_lock<std::mutex> l(lock_);
            TinyErrorCode ret = TinyErrorCode::SUCCESS;
            ResponseSignaled * rs = event_map_[event];
            TINY_ASSERT(rs->response != nullptr, "null response pointer");
            if (!rs->received) {
                bool out_of_time = false;
                if (timeout == 0) {
                    // wait forever
                    rs->cv.wait(l);
                }
                else {
                    std::cv_status s =
                        rs->cv.wait_for(l, std::chrono::milliseconds(timeout));
                    if (s == std::cv_status::timeout) {
                        out_of_time = true;
                    }
                }
                if (out_of_time) {
                    ret = TinyErrorCode::TIMEOUT;
                }
                else if (rs->server_failure) {
                    ret = TinyErrorCode::SERVER_FAIL;
                }
            }
            
            RemoveEventLocked(event);
            return ret;
        }

        Response * GetResponsePtr(int64_t event) {
            LockGuard l(lock_);
            auto it = event_map_.find(event);
            if (it == event_map_.end()) {
                // could have timed out and deleted
                return nullptr;
            }
            else {
                return it->second->response;
            }
        }

        void SignalResponse(int64_t event) {
            LockGuard l(lock_);
            auto it = event_map_.find(event);
            if (it == event_map_.end()) {
                // could have timed out and deleted
                return;
            }
            else {
                it->second->received = true;
                it->second->cv.notify_one();
            }
        }

        void SignalServerFail(int64_t event) {
            LockGuard l(lock_);
            auto it = event_map_.find(event);
            if (it == event_map_.end()) {
                // could have timed out and deleted
                return;
            }
            else {
                it->second->received = false;
                it->second->server_failure = true;
                it->second->cv.notify_one();
            }
        }
    private:
        void RemoveEventLocked(int64_t event) {
            // assuming lock is held
            auto it = event_map_.find(event);
            delete it->second;
            event_map_.erase(it);
        }

        std::map<int64_t, ResponseSignaled*> event_map_;
        std::mutex lock_;
    };

};

