#pragma once

#include <list>
#include "tinylock.h"
#include "stringstream.h"

namespace TinyRPC
{

class TinyMessageBuffer : public TinyStringStream
{
public:
	void set_remote_rank(int rank){_remote = rank;}
	int get_remote_rank(){return _remote;}
private:
	int _remote;
};

class TinyMessageQueue
{
public:
	TinyMessageQueue() : _kill_threads(false){}
	void push(TinyMessageBuffer* msg)
	{
		TinyAutoLock l(_lock);
		_messages.push_back(msg);
		if (_messages.size() == 1)
			_cond.signal();
	}
	TinyMessageBuffer * pop()
	{
		TinyAutoLock l(_lock);
		while (_messages.empty())
		{
			if (_kill_threads)
				return NULL;
			_cond.wait(l);			
		}
		TinyMessageBuffer * buf = _messages.front();
		_messages.pop_front();
		return buf;
	}
	void wake_all_for_kill()
	{
		// wake up every thread and return NULL so the threads can exit
		TinyAutoLock l(_lock);
		_kill_threads = true;
		_cond.broadcast();
	}
private:
	std::list<TinyMessageBuffer*> _messages;
	TinyLock _lock;
	TinyConditional _cond;
	bool _kill_threads;
};

};