#pragma once
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <thread>
#include <vector>

#include "protocol.h"
#include "tinycomm.h"
#include "tinylock.h"


namespace TinyRPC
{

    static const int NUM_WORKER_THREADS = 4;

    enum TinyErrorCode
    {
	    SUCCESS = 0,
	    KILLING_THREADS = 1
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

    template<class EndPointT>
    class TinyRPCStub
    {
	    typedef Message<EndPointT> MessageType;
        typedef std::shared_ptr<MessageType> MessagePtr;
        const static uint32_t RPC_ASYNC = 1;
        const static uint32_t RPC_SYNC = 0;

        struct MessageHeader
        {
            MessageHeader(){};
            MessageHeader(int64_t seq, uint32_t pid, uint32_t async)
                : seq_num(seq),
                protocol_id(pid),
                is_async(async)
            {}
            int64_t seq_num;
            uint32_t protocol_id;
            uint32_t is_async;
        };

    public:
        TinyRPCStub(TinyCommBase<EndPointT> * comm)
            : _comm(comm),
            _seq_num(1)
        {
            _comm->start();
            // start threads
            for (int i = 0; i< NUM_WORKER_THREADS; i++)
            {
                _worker_threads[i] = std::thread([this, i]()
                {
                    while (true)
                    {
                        MessagePtr msg = _comm->recv();
                        ASSERT(msg != nullptr, "worker %d received a null msg", i);
                        handle_message(msg);
                    }
                });
            }
        }

	    ~TinyRPCStub()
        {
            //TODO: should we kill the worker threads?
        }

	    // calls a remote function
        uint32_t rpc_call(const EndPointT & ep, ProtocolBase & protocol, bool is_async = false)
        {
		    MessagePtr message(new MessageType);
            // write header
            uint32_t async_rpc = is_async ? RPC_ASYNC : RPC_SYNC;
            int64_t seq = get_new_seq_num();
            Serialize(message->get_stream_buffer(), 
                MessageHeader(seq, protocol.get_id(), async_rpc));
            LOG("Calling rpc, seq=%lld, pid=%d, async=%d", seq, protocol.get_id(), async_rpc);
            protocol.marshall_request(message->get_stream_buffer());
            // send message
            message->set_remote_addr(ep);
            if (is_async)
            {
                _sleeping_list.set_response_ptr(seq, &protocol);
            }            
            _comm->send(message);
            // wait for signal
            if (is_async)
            {
                _sleeping_list.wait_for_response(seq);
            }
            return SUCCESS;
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
                ABORT("Duplicate protocol id detected: %d. "
                    "Did you registered the same protocol multiple times?", id);
		    }
		    _protocol_factory[id] = make_pair(new RequestFactory<T>(), app_server);
	    }
    private:
        // handle messages, called by WorkerFunction
        void handle_message(MessagePtr & msg)
        {
            MessageHeader header;
            Deserialize(msg->get_stream_buffer(), header);
            LOG("Handle message, seq=%lld, pid=%d, async=%d", 
                header.seq_num, header.protocol_id, header.is_async);

            if (header.seq_num < 0)
            {
                // negative seq number indicates a response to a sync rpc call
                header.seq_num = -header.seq_num;
                ProtocolBase * protocol = _sleeping_list.get_response_ptr(header.seq_num);
                protocol->unmarshall_response(msg->get_stream_buffer());
                // wake up waiting thread
                _sleeping_list.signal_response(header.seq_num);
            }
            else
            {
                // positive seq number indicates a request
                if (_protocol_factory.find(header.protocol_id) == _protocol_factory.end())
                {
                    ABORT("Unsupported protocol from %s, protocol ID=%d", 
                        EPToString(msg->get_remote_addr()).c_str(), header.protocol_id);
                    return;
                }
                ProtocolBase * protocol = _protocol_factory[header.protocol_id].first->create_protocol();
                protocol->unmarshall_request(msg->get_stream_buffer());
                protocol->handle_request(_protocol_factory[header.protocol_id].second);
                // send response if sync call
                if (!header.is_async)
                {
                    MessagePtr out_message(new MessageType);
                    header.seq_num = -header.seq_num;
                    Serialize(msg->get_stream_buffer(), header);
                    protocol->marshall_response(msg->get_stream_buffer());
                    out_message->set_remote_addr(msg->get_remote_addr());
                    LOG("responding to %s with seq=%d, protocol_id=%d\n", 
                        EPToString(msg->get_remote_addr()).c_str(), header.seq_num, header.protocol_id);
                    _comm->send(out_message);
                }
                delete protocol;
            }
        }

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
	    std::vector<std::thread> _worker_threads;
	    // sequence number
	    int64_t _seq_num;
	    TinyLock _seq_lock;
	    // waiting queue
	    SleepingList<ProtocolBase> _sleeping_list;
    };	
};

