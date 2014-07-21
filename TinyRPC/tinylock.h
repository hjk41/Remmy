#pragma once

#include <map>
#include <set>
#include <stdint.h>

#ifdef WIN32
#define USE_CPP_LOCK
#endif


#ifdef USE_CPP_LOCK
#include <condition_variable>
#include <mutex>
#include <chrono>
#endif



namespace TinyRPC
{

#ifdef USE_CPP_LOCK
typedef std::mutex TinyLock;

typedef std::unique_lock<std::mutex> TinyAutoLock;

class TinyConditional
{
public:
	void wait(TinyLock & lock)
	{
		_cond.wait(TinyAutoLock(lock));
	}
	void wait(TinyAutoLock & lock)
	{
		_cond.wait(lock);
	}
	void signal()
	{
		_cond.notify_one();
	}
	void broadcast()
	{
		_cond.notify_all();
	}
private:
	std::condition_variable _cond;
};

#else


#endif

template<class Response>
class SleepingList
{
public:
	SleepingList() : _kill_threads(false){}
	void set_response_ptr(int64_t event, Response * r)
	{
		TinyAutoLock l(_lock);
		_list[event] = std::make_pair(false, r);
	}
	void wait_for_response(int64_t event)
	{
		TinyAutoLock l(_lock);
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
		TinyAutoLock l(_lock);
		return _list[event].second;
	}
	void signal_response(int64_t event)
	{
		TinyAutoLock l(_lock);
		_list[event].first = true;
		_wake_cond.broadcast();
	}
	void wake_all_for_killing()
	{
		TinyAutoLock l(_lock);
		_kill_threads = true;
		_wake_cond.broadcast();
	}
private:
	std::map<int64_t, std::pair<bool, Response*> > _list;
	TinyLock _lock;
	TinyConditional _wake_cond;
	volatile bool _kill_threads;
};

};

