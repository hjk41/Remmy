#pragma once
#include <vector>
#include <iostream>
#include <map>
#include <list>

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include "stringstream.h"
#include "tinylock.h"
#include "tinythread.h"
#include "tinymessagequeue.h"
#include "singleton.h"
#include "tinycomm.h"

namespace TinyRPC
{

enum TinyErrorCode
{
	SUCCESS = 0,
	KILLING_THREADS = 1
};

static const int NUM_WORKER_THREADS = 4;

class ProtocolBase
{
public:
	virtual std::ostream & marshall_request(std::ostream & os)=0;
	virtual std::istream & unmarshall_request(std::istream & is)=0; 
	virtual void handle_request(void * server) =0;
	virtual std::ostream & marshall_response(std::ostream & os) =0;
	virtual std::istream & unmarshall_response(std::istream & is)=0;
	virtual uint32_t ID()=0;
};

template<class RequestT, class ResponseT>
class ProtocolTemplate : public ProtocolBase
{
public:
	virtual std::ostream & marshall_request(std::ostream & os)
	{
		Marshall(os, request);
		return os;
	};
	virtual std::istream & unmarshall_request(std::istream & is)
	{
		UnMarshall(is, request);
		return is;
	};
	virtual std::ostream & marshall_response(std::ostream & os)
	{
		Marshall(os, response);
		return os;
	}
	virtual std::istream & unmarshall_response(std::istream & is)
	{
		UnMarshall(is, response);
		return is;
	}
public:
	RequestT request;
	ResponseT response;
};

class RequestFactoryBase
{
public:
	virtual ProtocolBase * create_protocol()=0;
};

template<class T>
class RequestFactory : public RequestFactoryBase
{
public:
	virtual T * create_protocol(){return new T;};
};

typedef std::map<uint32_t, std::pair<RequestFactoryBase *, void*> > RequestFactories;

class TinyRPCStub : public Singleton<TinyRPCStub>
{
public:
	~TinyRPCStub();
	// initialize communication, etc.
	void init(int * argc, char *** argv);
	// destroy
	void destroy();
	// start processing messages
	void start_serving();

	// calls a remote function
	uint32_t rpc_call(int who, ProtocolBase & protocol);
	uint32_t rpc_call_async(int who, ProtocolBase & protocol);
	// handle messages, called by WorkerFunction
	void handle_message();

	// inline functions
	inline void barrier(){_comm->barrier();}
	inline int get_num_nodes(){return _comm->get_num_nodes();}
	inline int get_node_id(){return _comm->get_node_id();}
	template<class T, void * app_server>
	void RegisterProtocol()
	{
		T * t = new T;
		uint32_t id = t->ID();
		delete t;
		if (_protocol_factory.find(id) != _protocol_factory.end())
		{
			// ID() should be unique, and should not be re-registered
			assert(0);
		}
		_protocol_factory[id] = make_pair(new RequestFactory<T>(), app_server);
	}
private:
	int64_t get_new_seq_num();
private:
	TinyCommMPI * _comm;
	// for request handling
	RequestFactories _protocol_factory;
	// threads
	std::vector<TinyThread *> _worker_threads;
	// sequence number
	int64_t _seq_num;
	TinyLock _seq_lock;
	// waiting queue
	SleepingList<ProtocolBase> _sleeping_list;
	bool _kill_threads;
};

};