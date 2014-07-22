#pragma once
#include <vector>
#include <iostream>
#include <map>
#include <list>

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include "protocol.h"
#include "tinylock.h"
#include "tinythread.h"
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

template<class EndPointT>
class TinyRPCStub
{
	typedef Message<EndPointT> MessageType;
    typedef std::shared_ptr<MessageType> MessagePtr;

    static const uint32_t ASYNC_RPC_CALL = 1;
    static const uint32_t SYNC_RPC_CALL = 0;
public:
    TinyRPCStub(TinyCommBase<EndPointT> * comm)
        : _comm(comm),
        _seq_num(1),
        _kill_threads(false)
    {
    }

	~TinyRPCStub()
    {
    }
    
	// start processing messages
	void start_serving()
    {
	    _comm->start();
		// start threads
	    for (int i=0; i< NUM_WORKER_THREADS; i++)
	    {
		    TinyThread * worker = new TinyThread(WorkerFunction, this, &_kill_threads);
		    _worker_threads.push_back(worker);
	    }
    }

    // stop serving
    void stop_serving()
    {
        _kill_threads = true;
        _sleeping_list.wake_all_for_killing();
        for (int i = 0; i< NUM_WORKER_THREADS; i++)
        {
            _worker_threads[i]->wait();
            delete _worker_threads[i];
        }
    }

	void barrier()
	{
		while (true);
	}

	// calls a remote function
	uint32_t rpc_call(int who, ProtocolBase & protocol)
    {
		MessagePtr message(new MessageType);
        int64_t seq = get_new_seq_num();
		message->set_seq(seq);
		message->set_protocol_id(protocol.get_id());
		message->set_sync(SYNC_RPC_CALL);
		message->set_stream_buffer(protocol.get_buf());
		boost::asio::ip::address addr;
		message->set_remote_addr(asioEP(addr.from_string("10.190.172.62"), TEST_PORT));
        // send message
        _sleeping_list.set_response_ptr(seq, &protocol);
        _comm->send(message);
        // wait for signal
        _sleeping_list.wait_for_response(seq);
        if (_kill_threads)
            return KILLING_THREADS;
        return SUCCESS;
    }

	uint32_t rpc_call_async(int who, ProtocolBase & protocol)
    {
		MessagePtr message(new MessageType);
        int64_t seq = get_new_seq_num();
		message->set_seq(seq);
		message->set_protocol_id(protocol->get_id());
		message->set_sync(ASYNC_RPC_CALL);
		message->set_stream_buffer(protocol->get_buf());
		message->set_remote_addr(asioEP(boost::asio::ip::tcp::v4(), TEST_PORT));
        // send message
        _comm->send(message);
        return SUCCESS;
    }

	// handle messages, called by WorkerFunction
    //---------------------------------
    // message format:
    //      int64_t seq_num:		positive means request, negtive means response
    //		uint32_t protocol_id:	protocol id, must be registered in both client and server
    //	for request
    //		uint32_t async:			0 means sync call, 1 means async call
    //		char * buf:				request of the protocol
    //	for response
    //		char * buf:				response of the protocol
	void handle_message()
    {
        MessagePtr message = _comm->recv();
        if (_kill_threads)
            return;
        // get seq number, test if request or response
        int64_t seq = message->get_seq();
        // get the protocol handle
        uint32_t protocol_id = message->get_protocol_id();

        if (seq < 0)
        {
			cout << "get a response" << endl;
            // a response
            seq = -seq;
            // get response
            ProtocolBase * protocol = _sleeping_list.get_response_ptr(seq);
            if (_kill_threads)
                return;
			protocol->get_response(message->get_stream_buffer());
            //delete message;
            // wake up waiting thread
            _sleeping_list.signal_response(seq);
        }
        else
        {
			cout << "get a request" << endl;
            if (_protocol_factory.find(protocol_id) == _protocol_factory.end())
            {
                // unsupported call, register the func with the server please!
                ABORT("Unsupported call from %d, request ID=%d", message->get_remote_addr(), protocol_id);
				//delete message;
                return;
            }
            ProtocolBase * protocol = _protocol_factory[protocol_id].first->create_protocol();
            // a request
			uint32_t is_async = message->get_sync();
            // handle request
            protocol->handle_request(message->get_stream_buffer());
            // send response if sync call
            if (!is_async)
            {
                MessagePtr out_message(new MessageType);
				out_message->set_seq(-seq);
				out_message->set_sync(is_async);
				out_message->set_protocol_id(protocol_id);
				out_message->set_stream_buffer(protocol->get_buf());
				out_message->set_remote_addr(message->get_remote_addr());
                LOG("responding to %s:%d with seq=%d, protocol_id=%d\n", message->get_remote_addr().address(), message->get_remote_addr().port(), -seq, protocol_id);
				_comm->send(out_message);
            }
            //delete out_message;
            delete protocol;
        }
    }

	template<class T, void * app_server>
	void RegisterProtocol()
	{
		T * t = new T;
		uint32_t id = t->get_id();
		delete t;
		if (_protocol_factory.find(id) != _protocol_factory.end())
		{
			// ID() should be unique, and should not be re-registered
			assert(0);
		}
		_protocol_factory[id] = make_pair(new RequestFactory<T>(), app_server);
	}
private:
    int64_t get_new_seq_num()
    {
        TinyAutoLock l(_seq_lock);
        if (_seq_num >= INT64_MAX - 1)
            _seq_num = 1;
        return _seq_num++;
    }
private:
	TinyCommBase<EndPointT> * _comm;
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

	void WorkerFunction(void * rpc_ptr, void * kill_ptr)
	{
		TinyRPCStub<asioEP> * rpc = static_cast<TinyRPCStub<asioEP> *>(rpc_ptr);
		bool * kill_threads = static_cast<bool *>(kill_ptr);
		while(*kill_threads == false)
		{
			rpc->handle_message();
		}
	}	
};

