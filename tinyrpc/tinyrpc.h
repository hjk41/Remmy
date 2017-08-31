#pragma once
#include <atomic>
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <thread>
#include <vector>

#include "comm_asio.h"
#include "comm_zmq.h"
#include "logging.h"
#include "protocol.h"
#include "set_thread_name.h"
#include "sleeplist.h"
#include "tinycomm.h"
#include "tinydatatypes.h"
#include "unique_id.h"

namespace tinyrpc {

    class ProtocolFactoryBase {
    public:
        virtual ~ProtocolFactoryBase() {}
        virtual ProtocolBase* CreateProtocol()=0;
    };

    template<class T, class... Args>
    class ProtocolFactory : public ProtocolFactoryBase {
        std::tuple<Args...> args_;
    public:
        ProtocolFactory(Args... args) : args_(args...) {}

        template <size_t... I>
        T* create_protocol(std::index_sequence<I...>) {
            return new T(std::get<I>(args_)...);
        }

        virtual T* CreateProtocol(){ return create_protocol(std::index_sequence_for<Args...>()); };
    };

    struct ProtocolFactoryItem {
        ProtocolFactoryItem() {}
        ProtocolFactoryItem(ProtocolFactoryBase* f, void* s, std::function<void(void)> t) 
            : factory(f), handler(s), handler_delete(t) {}
        ProtocolFactoryBase* factory;         // factory pointer
        void* handler;                       // handler pointer
        std::function<void(void)> handler_delete;    // handler free function
    };

    typedef std::map<uint64_t, ProtocolFactoryItem > ProtocolFactories;

    template<class EndPointT>
    class TinyRPCStub {
        typedef Message<EndPointT> MessageType;
        typedef std::shared_ptr<MessageType> MessagePtr;
        const static uint32_t RPC_ASYNC = 1;
        const static uint32_t RPC_SYNC = 0;

        #pragma pack(push, 1)
        struct MessageHeader {
            int64_t seq_num;
            uint64_t protocol_id;
            uint32_t is_async;
        };
        #pragma pack(pop)
    public:
        TinyRPCStub(TinyCommBase<EndPointT> * comm, int num_workers = 1)
            : comm_(comm),
            seq_num_(1),
            worker_threads_(num_workers),
            exit_now_(false),
            serving_(false) {
            // start threads
            for (int i = 0; i< num_workers; i++) {
                worker_threads_[i] = std::thread([this, i]() {
                    SetThreadName("RPC worker ", i);
                    while (!exit_now_) {
                        MessagePtr msg = comm_->Recv();
                        if (msg == nullptr) {
                            TINY_LOG("RPC worker %d exiting", i);
                            return;
                        }
                        HandleMessage(msg);
                    }
                });
            }
        }

        ~TinyRPCStub() {
            comm_->SignalHandlerThreadsToExit();
            for (auto & thread : worker_threads_) {
                thread.join();
            }
            for (auto& p : protocol_factory_) {
                delete p.second.factory;
                p.second.handler_delete();
            }
        }

        void StartServing() {
            comm_->Start();
            serving_ = true;
        }

        #if 0
        // calls a remote function
        TinyErrorCode RpcCall(const EndPointT & ep, ProtocolBase & protocol, uint64_t timeout = 0, bool is_async = false) {
            TINY_ASSERT(serving_, "TinyRPCStub::StartServing() must be called before RpcCall");
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = protocol.UniqueId();
            header.is_async = is_async ? RPC_ASYNC : RPC_SYNC;
            Serialize(message->GetStreamBuffer(), header);
            TINY_LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
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
                TINY_WARN("error during rpc_call-send: %d", err);
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
        #endif

        template<uint64_t uid, typename ResponseT, typename... RequestTs>
        TinyErrorCode RpcCall(const EndPointT& ep,
            uint64_t timeout,
            ResponseT& resp,
            const RequestTs&... reqs) {
            TINY_ASSERT(serving_, "TinyRPCStub::StartServing() must be called before RpcCall");
            bool is_async = false;
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = uid;
            header.is_async = is_async ? RPC_ASYNC : RPC_SYNC;
            Serialize(message->GetStreamBuffer(), header);
            TINY_LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
            SerializeVariadic(message->GetStreamBuffer(), reqs...);
            // send message
            message->SetRemoteAddr(ep);
            SyncProtocol<uid, ResponseT, RequestTs...> protocol;
            if (!is_async) {
                sleeping_list_.AddEvent(header.seq_num, &protocol);
                LockGuard l(waiting_event_lock_);
                ep_waiting_events_[ep].insert(header.seq_num);
            }
            CommErrors err = comm_->Send(message);
            if (err != CommErrors::SUCCESS) {
                TINY_WARN("error during rpc_call-send: %d", err);
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
            resp = std::move(protocol.response);
            return c;
        }

		template<uint64_t uid, typename... RequestTs>
        TinyErrorCode RpcCallAsync(const EndPointT& ep,
			const RequestTs&... reqs) {
            bool is_async = true;
            TINY_ASSERT(serving_, "TinyRPCStub::StartServing() must be called before RpcCall");
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = uid;
            header.is_async = is_async;
            Serialize(message->GetStreamBuffer(), header);
            TINY_LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
            SerializeVariadic(message->GetStreamBuffer(), reqs...);
            // send message
            message->SetRemoteAddr(ep);
            CommErrors err = comm_->Send(message);
            if (err != CommErrors::SUCCESS) {
                TINY_WARN("error during rpc_call-send: %d", err);
                sleeping_list_.RemoveEvent(header.seq_num);
                return TinyErrorCode::FAIL_SEND;
            }
            return TinyErrorCode::SUCCESS;
        }

        template <typename T>
        struct _identity {
            typedef T type;
        };

        template<uint64_t UID, typename ResponseT, typename... RequestTs>
        void RegisterSyncHandler(typename _identity<std::function<ResponseT(RequestTs&...)>>::type func) {
            using CallbackT = std::function<ResponseT(RequestTs&...)>;
            if (protocol_factory_.find(UID) != protocol_factory_.end()) {
                // ID() should be unique, and should not be re-registered
                TINY_ABORT("Duplicate protocol id detected: %d for %s. "
                    "Did you registered the same protocol multiple times?",
                    UID, DecodeUniqueId(UID).c_str());
            }
            CallbackT* fp = new CallbackT(func);
            protocol_factory_[UID] =
                ProtocolFactoryItem(new ProtocolFactory<SyncProtocol<UID, ResponseT, RequestTs...>>(),
                (void*)fp,
                    [=]() {delete fp; });
        }

        template<uint64_t UID, typename... RequestTs>
        void RegisterAsyncHandler(typename _identity<std::function<void(RequestTs&...)>>::type func) {
            using CallbackT = std::function<void(RequestTs&...)>;
            if (protocol_factory_.find(UID) != protocol_factory_.end()) {
                // ID() should be unique, and should not be re-registered
                TINY_ABORT("Duplicate protocol id detected: %d for %s. "
                    "Did you registered the same protocol multiple times?",
                    UID, DecodeUniqueId(UID).c_str());
            }
            CallbackT* fp = new CallbackT(func);
            protocol_factory_[UID] =
                ProtocolFactoryItem(new ProtocolFactory<AsyncProtocol<UID, RequestTs...>>(),
                (void*)fp,
                    [=]() {delete fp; });
        }

        template<typename... RequestTs>
        void RegisterAsyncHandlerReplaceable(size_t UID, typename _identity<std::function<void(RequestTs...)>>::type func) {
            auto it = protocol_factory_.find(UID);
            auto new_factory = new ProtocolFactory<AsyncProtocolReplaceable<RequestTs...>, decltype(UID), decltype(func)>(UID, func);
            if ( it != protocol_factory_.end()) {
                delete it->second.factory;
                it->second.factory = new_factory;
            } else
                protocol_factory_[UID] = ProtocolFactoryItem(new_factory, nullptr, []() {});
        }
    private:
        // handle messages, called by WorkerFunction
        void HandleMessage(MessagePtr& msg) {
            if (msg->GetStatus() != TinyErrorCode::SUCCESS) {
                TINY_WARN("RPC get a message of communication failure of machine %s, status=%d",
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
            TINY_LOG("Handle message, seq=%lld, pid=%d, async=%d",
                header.seq_num, header.protocol_id, header.is_async);

            if (header.seq_num < 0) {
                // negative seq number indicates a response to a sync rpc call
                header.seq_num = -header.seq_num;
                ProtocolBase * protocol = sleeping_list_.GetResponsePtr(header.seq_num);
                if (protocol != nullptr) {
                    // null protocol indicates this request already timedout and removed
                    // so we don't need to get the response or signal the thread
                    protocol->UnmarshallResponse(msg->GetStreamBuffer());
                    TINY_ASSERT(msg->GetStreamBuffer().GetSize() == 0,
                        "Error unmarshalling response of protocol %s: "
                        "%llu bytes are left unread",
                        DecodeUniqueId(protocol->UniqueId()).c_str(),
                        msg->GetStreamBuffer().GetSize());
                    sleeping_list_.SignalResponse(header.seq_num);
                }
            }
            else {
                // positive seq number indicates a request
                if (protocol_factory_.find(header.protocol_id) == protocol_factory_.end()) {
                    TINY_ABORT("Unsupported protocol from %s, protocol ID=%d",
                        EPToString(msg->GetRemoteAddr()).c_str(), header.protocol_id);
                    return;
                }
                ProtocolBase* protocol =
                    protocol_factory_[header.protocol_id].factory->CreateProtocol();
                protocol->UnmarshallRequest(msg->GetStreamBuffer());
                TINY_ASSERT(msg->GetStreamBuffer().GetSize() == 0,
                    "Error unmarshalling request of protocol %s: %llu bytes are left unread",
                    DecodeUniqueId(protocol->UniqueId()).c_str(),
                    msg->GetStreamBuffer().GetSize());
                protocol->HandleRequest(protocol_factory_[header.protocol_id].handler);
                // send response if sync call
                if (!header.is_async) {
                    MessagePtr out_message(new MessageType);
                    header.seq_num = -header.seq_num;
                    Serialize(out_message->GetStreamBuffer(), header);
                    protocol->MarshallResponse(out_message->GetStreamBuffer());
                    out_message->SetRemoteAddr(msg->GetRemoteAddr());
                    TINY_LOG("responding to %s with seq=%d, protocol_id=%d\n",
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
        ProtocolFactories protocol_factory_;
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
        // have StartServing been called?
        std::atomic<int> serving_;
    };
};

