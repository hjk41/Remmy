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

namespace tinyrpc {

    class RequestFactoryBase {
    public:
        virtual ProtocolBase * CreateProtocol()=0;
    };

    template<class T>
    class RequestFactory : public RequestFactoryBase {
    public:
        virtual T * CreateProtocol(){return new T;};
    };

    typedef std::map<uint32_t, std::pair<RequestFactoryBase *, void*> > RequestFactories;

    template<class EndPointT>
    class TinyRPCStub {
        typedef Message<EndPointT> MessageType;
        typedef std::shared_ptr<MessageType> MessagePtr;
        const static uint32_t RPC_ASYNC = 1;
        const static uint32_t RPC_SYNC = 0;

        struct MessageHeader {
            int64_t seq_num;
            uint32_t protocol_id;
            uint32_t is_async;
        };

    public:
        TinyRPCStub(TinyCommBase<EndPointT> * comm, int num_workers = 1)
            : comm_(comm),
            seq_num_(1),
            worker_threads_(num_workers),
            exit_now_(false) {
            comm_->Start();
            // start threads
            for (int i = 0; i< num_workers; i++) {
                worker_threads_[i] = std::thread([this, i]() {
                    SetThreadName("RPC worker ", i);
                    while (!exit_now_) {
                        MessagePtr msg = comm_->Recv();
                        if (msg == nullptr) {
                            LOG("RPC worker %d exiting", i);
                            return;
                        }
                        HandleMessage(msg);
                    }
                });
            }
        }

        ~TinyRPCStub() {
            comm_->Stop();
            for (auto & thread : worker_threads_) {
                thread.join();
            }
            // The _comm does not belong to us. It is the caller's responsibility
            // to destruct the _comm.
        }

        // calls a remote function
        TinyErrorCode RpcCall(const EndPointT & ep, ProtocolBase & protocol, uint64_t timeout = 0, bool is_async = false) {
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = protocol.UniqueId();
            header.is_async = is_async ? RPC_ASYNC : RPC_SYNC;
            Serialize(message->GetStreamBuffer(), header);
            LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
            protocol.MarshallRequest(message->GetStreamBuffer());
            // send message
            message->SetRemoteAddr(ep);
            if (!is_async) {
                sleeping_list_.AddEvent(header.seq_num, &protocol);
                LockGuard l(waiting_event_lock_);
                ep_waiting_events_[ep].insert(header.seq_num);
            }
            CommErrors err = comm_->Send(message);
            if (err != CommErrors::SUCCESS) {
                WARN("error during rpc_call-send: %d", err);
                sleeping_list_.RemoveEvent(header.seq_num);
                return TinyErrorCode::FAIL_SEND;
            }
            // wait for signal
            if (is_async) {
                return TinyErrorCode::SUCCESS;
            }
            
            TinyErrorCode c = sleeping_list_.WaitForResponse(header.seq_num, timeout);
            {
                LockGuard l(waiting_event_lock_);
                ep_waiting_events_[ep].erase(header.seq_num);
            }            
            return c;
        }

        template<class T>
        void RegisterProtocol(void * app_server) {
            T * t = new T;
            uint32_t id = t->UniqueId();
            delete t;
            if (protocol_factory_.find(id) != protocol_factory_.end()) {
                // ID() should be unique, and should not be re-registered
                ABORT("Duplicate protocol id detected: %d. "
                    "Did you registered the same protocol multiple times?", id);
            }
            protocol_factory_[id] = std::make_pair(new RequestFactory<T>(), app_server);
        }
    private:
        // handle messages, called by WorkerFunction
        void HandleMessage(MessagePtr & msg) {
            if (msg->GetStatus() != TinyErrorCode::SUCCESS) {
                WARN("RPC get a message of communication failure of machine %s, status=%d",
                    EPToString(msg->GetRemoteAddr()).c_str(), msg->GetStatus());
                const EndPointT & ep = msg->GetRemoteAddr();
                std::set<int64_t> events;
                {
                    LockGuard l(waiting_event_lock_);
                    auto it = ep_waiting_events_.find(ep);
                    if (it != ep_waiting_events_.end())
                    {
                        events = it->second;
                        ep_waiting_events_.erase(it);
                    }
                }
                for (auto & event : events) {
                    sleeping_list_.SignalServerFail(event);
                }
                return;
            }

            MessageHeader header;
            Deserialize(msg->GetStreamBuffer(), header);
            LOG("Handle message, seq=%lld, pid=%d, async=%d", 
                header.seq_num, header.protocol_id, header.is_async);

            if (header.seq_num < 0) {
                // negative seq number indicates a response to a sync rpc call
                header.seq_num = -header.seq_num;
                ProtocolBase * protocol = sleeping_list_.GetResponsePtr(header.seq_num);
                if (protocol != nullptr) {
                    // null protocol indicates this request already timedout and removed
                    // so we don't need to get the response or signal the thread
                    protocol->UnmarshallResponse(msg->GetStreamBuffer());
                    sleeping_list_.SignalResponse(header.seq_num);
                }                
            }
            else {
                // positive seq number indicates a request
                if (protocol_factory_.find(header.protocol_id) == protocol_factory_.end()) {
                    ABORT("Unsupported protocol from %s, protocol ID=%d", 
                        EPToString(msg->GetRemoteAddr()).c_str(), header.protocol_id);
                    return;
                }
                ProtocolBase * protocol = protocol_factory_[header.protocol_id].first->CreateProtocol();
                protocol->UnmarshallRequest(msg->GetStreamBuffer());
                protocol->HandleRequest(protocol_factory_[header.protocol_id].second);
                // send response if sync call
                if (!header.is_async) {
                    MessagePtr out_message(new MessageType);
                    header.seq_num = -header.seq_num;
                    Serialize(out_message->GetStreamBuffer(), header);
                    protocol->MarshallResponse(out_message->GetStreamBuffer());
                    out_message->SetRemoteAddr(msg->GetRemoteAddr());
                    LOG("responding to %s with seq=%d, protocol_id=%d\n", 
                        EPToString(out_message->GetRemoteAddr()).c_str(), header.seq_num, header.protocol_id);
                    comm_->Send(out_message);
                }
                delete protocol;
            }
        }

        int64_t GetNewSeqNum() {
            LockGuard l(seq_lock_);
            if (seq_num_ >= INT64_MAX - 1)
                seq_num_ = 1;
            return seq_num_++;
        }
    private:
        TinyCommBase<EndPointT> * comm_;
        // for request handling
        RequestFactories protocol_factory_;
        // threads
        std::vector<std::thread> worker_threads_;
        // sequence number
        int64_t seq_num_;
        std::mutex seq_lock_;
        // waiting queue
        SleepingList<ProtocolBase> sleeping_list_;
        std::mutex waiting_event_lock_;
        std::unordered_map<EndPointT, std::set<int64_t>> ep_waiting_events_;
        // exit flag
        std::atomic<bool> exit_now_;
    };    
};

