#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <stdint.h>


namespace TinyRPC
{
    typedef std::lock_guard<std::mutex> LockGuard;

    template<class Response>
    class SleepingList
    {
    public:
	    SleepingList() : _kill_threads(false){}
	    void set_response_ptr(int64_t event, Response * r)
	    {
		    LockGuard l(_lock);
		    _list[event] = std::make_pair(false, r);
	    }
	    void wait_for_response(int64_t event)
	    {
		    std::unique_lock<std::mutex> l(_lock);
		    // if no response yet
		    while(_list[event].first == false)
		    {
			    if (_kill_threads)
				    return;
			    _wake_cond.wait(l);
		    }
		    _list.erase(event);
	    }
	    Response * get_response_ptr(int64_t event)
	    {
		    LockGuard l(_lock);
		    return _list[event].second;
	    }
	    void signal_response(int64_t event)
	    {
		    LockGuard l(_lock);
		    _list[event].first = true;
		    _wake_cond.notify_all();
	    }
	    void wake_all_for_killing()
	    {
		    LockGuard l(_lock);
		    _kill_threads = true;
		    _wake_cond.broadcast();
	    }
    private:
	    std::map<int64_t, std::pair<bool, Response*> > _list;
	    std::mutex _lock;
	    std::condition_variable _wake_cond;
	    volatile bool _kill_threads;
    };

};

