#pragma once
#include <vector>
#include <iostream>
#include <map>
#include <list>

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include "protocol.h"
#include "stringstream.h"
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
    static const uint32_t ASYNC_RPC_CALL = 1;
    static const uint32_t SYNC_RPC_CALL = 0;
public:
    TinyRPCStub(TinyCommBase<EndPointT> * comm)
        : _comm(comm),
        _seq_num(1),
        _kill_threads(false)
    {
    }

	~TinyRPCStub();
    {
    }

	// start processing messages
	void start_serving()
    {
	    _comm->start_polling();
	    // start threads
	    for (int i=0; i<NUM_WORKER_THREADS; i++)
	    {
		    TinyThread * worker = new TinyThread(WorkerFunction, this, &_kill_threads);
		    _worker_threads.push_back(worker);
	    }
    }

    // stop serving
    void stop_serving()
    {
        _kill_threads = true;
        _comm->wake_all_recv_threads();
        _sleeping_list.wake_all_for_killing();
        for (int i = 0; i<NUM_WORKER_THREADS; i++)
        {
            _worker_threads[i]->wait();
            delete _worker_threads[i];
        }
        _comm->finalize();
        _comm->delete_instance();
    }

	// calls a remote function
	uint32_t rpc_call(int who, ProtocolBase & protocol)
    {
        TinyMessageBuffer message;
        uint64_t seq = get_new_seq_num();
        Marshall(message, seq);
        Marshall(message, protocol.ID());
        Marshall(message, SYNC_RPC_CALL);
        protocol.marshall_request(message);
        // send message
        _sleeping_list.set_response_ptr(seq, &protocol);
        _comm->send(who, message);
        // wait for signal
        _sleeping_list.wait_for_response(seq);
        if (_kill_threads)
            return KILLING_THREADS;
        return SUCCESS;
    }

	uint32_t rpc_call_async(int who, ProtocolBase & protocol)
    {
        TinyMessageBuffer message;
        uint64_t seq = get_new_seq_num();
        Marshall(message, seq);
        Marshall(message, protocol.ID());
        Marshall(message, ASYNC_RPC_CALL);
        protocol.marshall_request(message);
        // send message
        _comm->send(who, message);
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
        TinyMessageBuffer * buf = _comm->recv();
        if (_kill_threads)
            return;
        // get seq number, test if request or response
        int64_t seq;
        UnMarshall(*buf, seq);
        // get the protocol handle
        uint32_t protocol_id;
        UnMarshall(*buf, protocol_id);

        if (seq < 0)
        {
            // a response
            seq = -seq;
            // unmarshal response
            ProtocolBase * protocol = _sleeping_list.get_response_ptr(seq);
            if (_kill_threads)
                return;
            protocol->unmarshall_response(*buf);
            delete buf;
            // wake up waiting thread
            _sleeping_list.signal_response(seq);
        }
        else
        {
            if (_protocol_factory.find(protocol_id) == _protocol_factory.end())
            {
                // unsupported call, register the func with the server please!
                int remote = buf->get_remote_rank();
                TinyLog(LOG_ERROR, "Unsupported call from %d, request ID=%d", remote, protocol_id);
                delete buf;
                return;
            }
            ProtocolBase * protocol = _protocol_factory[protocol_id].first->create_protocol();
            // a request
            uint32_t is_async;
            UnMarshall(*buf, is_async);
            // handle request
            protocol->unmarshall_request(*buf);
            protocol->handle_request(_protocol_factory[protocol_id].second);
            // send response if sync call
            if (!is_async)
            {
                TinyMessageBuffer out_buffer;
                Marshall(out_buffer, -seq);
                Marshall(out_buffer, protocol_id);
                protocol->marshall_response(out_buffer);
                TinyLog(LOG_NORMAL, "responding to %d with seq=%d, protocol_id=%d\n", buf->get_remote_rank(), -seq, protocol_id);
                _comm->send(buf->get_remote_rank(), out_buffer);
            }
            delete buf;
            delete protocol;
        }
    }

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

};