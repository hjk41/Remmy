#pragma once

#ifdef WIN32
#define USE_CPP_THREAD
#else

#endif

#ifdef USE_CPP_THREAD
#include <thread>
#else
#endif


namespace TinyRPC
{
typedef void ThreadFunction(void *);
typedef void ThreadFunction2(void*, void*);

#ifdef USE_CPP_THREAD

class TinyThread
{
public:
	TinyThread(ThreadFunction func, void * param) : _thread(func, param){};
	TinyThread(ThreadFunction2 func, void * param1, void * param2) : _thread(func, param1, param2){};
	~TinyThread(){};
	void start(){};
	void wait(){_thread.join();}
private:
	TinyThread(const TinyThread &);
private:
	std::thread _thread;
};

#else
class TinyThread
{
public:
	TinyThread(ThreadFunction func, void * param) : _func(func), _param(param){};
	~TinyThread(){};
	void start(){};
	void wait(){}
private:
	TinyThread(const TinyThread &);
private:
	ThreadFunction * _func;
	void * _param;
	void * _ret;
};
#endif

};