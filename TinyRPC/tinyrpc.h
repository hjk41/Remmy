#pragma once
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <thread>
#include <vector>

#include "protocol.h"
#include "sleeplist.h"
#include "tinycomm.h"
#include "tinydatatypes.h"

namespace TinyRPC
{

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
            int64_t seq_num;
            uint32_t protocol_id;
            uint32_t is_async;
        };

    public:
        TinyRPCStub(TinyCommBase<EndPointT> * comm, int num_workers)
            : _comm(comm),
            _seq_num(1),
            _worker_threads(num_workers),
            _exit_now_(false)
        {
            _comm->start();
            // start threads
            for (int i = 0; i< num_workers; i++)
            {
                _worker_threads[i] = std::thread([this, i]()
                {
                    SetThreadName("RPC worker ", i);
                    while (!_exit_now_)
                    {
                        MessagePtr msg = _comm->recv();
                        if (msg == nullptr)
                        {
                            LOG("RPC worker %d exiting", i);
                            return;
                        }
                        handle_message(msg);
                    }
                });
            }
        }

        ~TinyRPCStub()
        {
            _comm->WakeReceivingThreadsForExit();
            for (auto & thread : _worker_threads)
            {
                thread.join();
            }
        }

        // calls a remote function
        TinyErrorCode rpc_call(const EndPointT & ep, ProtocolBase & protocol, uint64_t timeout = 0, bool is_async = false)
        {
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = get_new_seq_num();
            header.protocol_id = protocol.get_id();
            header.is_async = is_async ? RPC_ASYNC : RPC_SYNC;
            Serialize(message->get_stream_buffer(), header);
            LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
            protocol.marshall_request(message->get_stream_buffer());
            // send message
            message->set_remote_addr(ep);
            if (!is_async)
            {
                _sleeping_list.add_event(header.seq_num, &protocol);
                LockGuard l(_waiting_event_lock);
                _ep_waiting_events[ep].insert(header.seq_num);
            }
            CommErrors err = _comm->send(message);
            if (err != CommErrors::SUCCESS)
            {
                WARN("error during rpc_call-send: %d", err);
                _sleeping_list.remove_event(header.seq_num);
                return TinyErrorCode::FAIL_SEND;
            }
            // wait for signal
            if (is_async)
            {
                return TinyErrorCode::SUCCESS;
            }
            
            TinyErrorCode c = _sleeping_list.wait_for_response(header.seq_num, timeout);
            {
                LockGuard l(_waiting_event_lock);
                _ep_waiting_events[ep].erase(header.seq_num);
            }            
            return c;
        }

        template<class T>
        void RegisterProtocol(void * app_server)
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
            _protocol_factory[id] = std::make_pair(new RequestFactory<T>(), app_server);
        }
    private:
        // handle messages, called by WorkerFunction
        void handle_message(MessagePtr & msg)
        {
            if (msg->get_status() != TinyErrorCode::SUCCESS)
            {
                WARN("RPC get a message of communication failure of machine %s, status=%d",
                    EPToString(msg->get_remote_addr()).c_str(), msg->get_status());
                EndPointT & ep = msg->get_remote_addr();
                std::set<int64_t> events;
                {
                    LockGuard l(_waiting_event_lock);
                    auto it = _ep_waiting_events.find(ep);
                    if (it != _ep_waiting_events.end())
                    {
                        events = it->second;
                        _ep_waiting_events.erase(it);
                    }
                }
                for (auto & event : events)
                {
                    _sleeping_list.signal_server_fail(event);
                }
                return;
            }

            MessageHeader header;
            Deserialize(msg->get_stream_buffer(), header);
            LOG("Handle message, seq=%lld, pid=%d, async=%d", 
                header.seq_num, header.protocol_id, header.is_async);

            if (header.seq_num < 0)
            {
                // negative seq number indicates a response to a sync rpc call
                header.seq_num = -header.seq_num;
                ProtocolBase * protocol = _sleeping_list.get_response_ptr(header.seq_num);
                if (protocol != nullptr)
                {
                    // null protocol indicates this request already timedout and removed
                    // so we don't need to get the response or signal the thread
                    protocol->unmarshall_response(msg->get_stream_buffer());
                    _sleeping_list.signal_response(header.seq_num);
                }                
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
                    Serialize(out_message->get_stream_buffer(), header);
                    protocol->marshall_response(out_message->get_stream_buffer());
                    out_message->set_remote_addr(msg->get_remote_addr());
                    LOG("responding to %s with seq=%d, protocol_id=%d\n", 
                        EPToString(out_message->get_remote_addr()).c_str(), header.seq_num, header.protocol_id);
                    _comm->send(out_message);
                }
                delete protocol;
            }
        }

        int64_t get_new_seq_num()
        {
            LockGuard l(_seq_lock);
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
        std::mutex _seq_lock;
        // waiting queue
        SleepingList<ProtocolBase> _sleeping_list;
        std::mutex _waiting_event_lock;
        std::unordered_map<EndPointT, std::set<int64_t>> _ep_waiting_events;
        // exit flag
        std::atomic<bool> _exit_now_;
    };    
};

