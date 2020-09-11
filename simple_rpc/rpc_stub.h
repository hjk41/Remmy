/**
 * \file    rpc_stub.h.
 *
 * Declares the RPCStub class, which is the main entry of the library.
 */
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
#include "comm.h"
#include "datatypes.h"
#include "unique_id.h"

namespace simple_rpc {

    /** A factory class used to create instances of protocols. This is an internal facility class. */
    class ProtocolFactoryBase {
    public:
        virtual ~ProtocolFactoryBase() {}
        virtual ProtocolBase* CreateProtocol()=0;
    };

    /**
     * A template factory that creates protocol instances of type T.
     *
     * \tparam T    Protocol type.
     * \tparam Args Type of the arguments used to create the protocol instance.
     */
    template<class T, class... Args>
    class ProtocolFactory : public ProtocolFactoryBase {
        std::tuple<Args...> args_;
    public:
        ProtocolFactory(Args... args) : args_(args...) {}

        template <size_t... I>
        T* create_protocol(std::index_sequence<I...>) {
            return new T(std::get<I>(args_)...);
        }

        /**
         * Creates a protocol instance.
         *
         * \return  Pointer to the newly created instance.
         */
        virtual T* CreateProtocol(){ return create_protocol(std::index_sequence_for<Args...>()); };
    };

    /** Each ProtocolFactoryItem stores:  
     *      1. a pointer to a factory class that produces a type of protocol instance  
     *      2. the handler to handle that protocol  
     *      3. a function to delete the handler
     */
    struct ProtocolFactoryItem {
        ProtocolFactoryItem() {}
        ProtocolFactoryItem(ProtocolFactoryBase* f, void* s, std::function<void(void)> t) 
            : factory(f), handler(s), handler_delete(t) {}

        /** pointer to a factory that produces a type of protocol instance */
        ProtocolFactoryBase* factory;
        /** handler to handle a protocol */
        void* handler;
        /** handler free function */
        std::function<void(void)> handler_delete;
    };

    /** A map that holds UID->ProtocolFactoryItem mapping. 
     * We associate each protocol with a unique id. When we send a RPC, we attach the UID to the head
     * of the message. So the receiver can tell which type of protocol the message contains. Then 
     * the receiver can query this map and get the corresponding factory, use that factory to deserialize
     * the message into a protocol instance, and call the corresponding handler. */
    typedef std::map<uint64_t, ProtocolFactoryItem > ProtocolFactories;

    /**
     * A RPC stub. This is the main entry to the RPC library. It implements the logic to RPC calls,
     * i.e., serialize the request, send the message, wait for response message, and deserilize the
     * response message.
     *
     * \tparam EndPointT    Type of the node address. Different communication library can choose to use
     *                      different types of EP. For example, ASIO may use HOST:IP, while MPI could
     *                      use an integer.
     */
    template<class EndPointT>
    class RPCStub {
        typedef Message<EndPointT> MessageType;
        typedef std::shared_ptr<MessageType> MessagePtr;
        const static uint32_t RPC_ASYNC = 1;
        const static uint32_t RPC_SYNC = 0;

        /** A message header contains meta info for this message. */
        #pragma pack(push, 1)
        struct MessageHeader {
            /** Each message is assigned a unique sequence id (unique w.r.t. this RPCStub). When sending
             *  a request, seq_num is assigned by the caller stub as a monotonically increasing integer.
             *  When sending a response to a request REQ, this number is the negative form of the unique
             *  id of REQ. */
            int64_t seq_num;

            /** Identifier for the protocol. Each protocol must have a unique ID so callee can tell which
             *  handler to call for this request. */
            uint64_t protocol_id;

            /** Is this an async call? An async call in RPC has no response. The caller just sends out
             *  the request and returns. If it needs to know the result, it must send a synchronous request
             *  or the callee must make a call so that the caller knows what happened. */
            uint32_t is_async;
        };
        #pragma pack(pop)
    public:
        /**
         * Constructor
         *
         * \param [in,out] comm         The communicator.
         * \param          num_workers  (Optional) Number of RPC worker threads.
         */
        RPCStub(CommBase<EndPointT> * comm, int num_workers = 1)
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
                            SIMPLE_LOG("RPC worker %d exiting", i);
                            return;
                        }
                        HandleMessage(msg);
                    }
                });
            }
        }

        ~RPCStub() {
            comm_->SignalHandlerThreadsToExit();
            for (auto & thread : worker_threads_) {
                thread.join();
            }
            for (auto& p : protocol_factory_) {
                delete p.second.factory;
                if (p.second.handler_delete) p.second.handler_delete();
            }
        }

        /** Starts serving for RPC calls */
        void StartServing() {
            comm_->Start();
            serving_ = true;
        }

        /**
         * Issues a RPC call for a function: ResponseT rpc_call(RequestT1 param1, RequestT2 param2, ...)
         *
         * \param          ep       The callee's address.
         * \param [in,out] protocol The protocol.
         * \param          timeout  (Optional) Timeout in milliseconds. 0 if no timeout.
         * \param          is_async (Optional) The response.
         *
         * \return  A ErrorCode.
         */
        ErrorCode RpcCall(const EndPointT & ep, ProtocolBase & protocol, uint64_t timeout = 0, bool is_async = false) {
            SIMPLE_ASSERT(serving_, "RPCStub::StartServing() must be called before RpcCall");
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = protocol.UniqueId();
            header.is_async = is_async ? RPC_ASYNC : RPC_SYNC;
            Serialize(message->GetStreamBuffer(), header);
            SIMPLE_LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
            protocol.MarshallRequest(message->GetStreamBuffer());
            // send message
            message->SetRemoteAddr(ep);
            if (!is_async) {
                sleeping_list_.AddEvent(header.seq_num, &protocol);
                {
                    LockGuard l(waiting_event_lock_);
                    ep_waiting_events_[ep].insert(header.seq_num);
                }
                CommErrors err = comm_->Send(message);
                if (err != CommErrors::SUCCESS) {
                    SIMPLE_WARN("error during rpc_call-send: %d", err);
                    sleeping_list_.RemoveEvent(header.seq_num);
                    return ErrorCode::FAIL_SEND;
                }
                // wait for response
                ErrorCode c = sleeping_list_.WaitForResponse(header.seq_num, timeout);
                {
                    LockGuard l(waiting_event_lock_);
                    ep_waiting_events_[ep].erase(header.seq_num);
                }
                return c;
            }
            else {
                // async, just call asyncSend and return
                comm_->AsyncSend(message, nullptr);
                return ErrorCode::SUCCESS;
            }
        }

        /**
         * Issues a RPC call for a function: ResponseT rpc_call(RequestT1 param1, RequestT2 param2, ...)
         *
         * \tparam uid          UID of the protocol
         * \tparam ResponseT    Type of the response.
         * \tparam RequestTs    Type of the request parameters. 
         * \param          ep       The callee's address.
         * \param          timeout  Timeout in milliseconds. 0 if no timeout.
         * \param [in,out] resp     The response.
         * \param          reqs     Request parameters.
         *
         * \return  A ErrorCode.
         */
        template<uint64_t uid, typename ResponseT, typename... RequestTs>
        ErrorCode RpcCall(const EndPointT& ep,
            uint64_t timeout,
            ResponseT& resp,
            const RequestTs&... reqs) {
            SIMPLE_ASSERT(serving_, "RPCStub::StartServing() must be called before RpcCall");
            bool is_async = false;
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = uid;
            header.is_async = is_async ? RPC_ASYNC : RPC_SYNC;
            Serialize(message->GetStreamBuffer(), header);
            SIMPLE_LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
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
                SIMPLE_WARN("error during rpc_call-send: %d", err);
                sleeping_list_.RemoveEvent(header.seq_num);
                return ErrorCode::FAIL_SEND;
            }
            // wait for signal
            if (is_async) {
                return ErrorCode::SUCCESS;
            }

            ErrorCode c = sleeping_list_.WaitForResponse(header.seq_num, timeout);
            {
                LockGuard l(waiting_event_lock_);
                ep_waiting_events_[ep].erase(header.seq_num);
            }
            resp = std::move(protocol.response);
            return c;
        }

        /**
         * Issues an asynchronous RPC call for a function: void rpc_call(RequestT1 param1, RequestT2 param2, ...)
         *
         * \tparam uid          UID of the protocol
         * \tparam RequestTs    Type of the request parameters.
         * \param ep    The address of the callee.
         * \param reqs  Request parameters.
         *
         * \return  A ErrorCode.
         */
		template<uint64_t uid, typename... RequestTs>
        ErrorCode RpcCallAsync(const EndPointT& ep,
			const RequestTs&... reqs) {
            SIMPLE_ASSERT(serving_, "RPCStub::StartServing() must be called before RpcCall");
            MessagePtr message(new MessageType);
            // write header
            MessageHeader header;
            header.seq_num = GetNewSeqNum();
            header.protocol_id = uid;
            header.is_async = true;
            Serialize(message->GetStreamBuffer(), header);
            SIMPLE_LOG("Calling rpc, seq=%lld, pid=%d, async=%d", header.seq_num, header.protocol_id, header.is_async);
            SerializeVariadic(message->GetStreamBuffer(), reqs...);
            // send message
            message->SetRemoteAddr(ep);
            comm_->AsyncSend(message, nullptr);
            return ErrorCode::SUCCESS;
        }

        template <typename T>
        struct _identity {
            typedef T type;
        };

        /**
         * Registers a protocol
         *
         * \tparam ProtocolT    Type of the protocol.
         * \param [in,out] server   Server pointer, as passed to Protocol::HandleRequest().
         */
        template<typename ProtocolT>
        void RegisterProtocol(void* server) {
            uint64_t UID = ProtocolT().UniqueId();
            if (protocol_factory_.find(UID) != protocol_factory_.end()) {
                // ID() should be unique, and should not be re-registered
                SIMPLE_ABORT("Duplicate protocol id detected: %d for %s. "
                           "Did you registered the same protocol multiple times?",
                           UID, DecodeUniqueId(UID).c_str());
            }
            protocol_factory_[UID] =
                ProtocolFactoryItem(new ProtocolFactory<ProtocolT>(), server, nullptr);
        }

        /**
         * Registers a synchronous call handler to a protocol with UID.
         * 
         * \note This is NOT thread-safe. It should not happen in parallel with RPC call or other
         *       register operations.
         *
         * \tparam UID          UID of the protocol.
         * \tparam ResponseT    Type of the response.
         * \tparam RequestTs    Type of the request parameters.
         * \param [in,out] func The handler, must have form std::function<ResponseT(RequestTs...)>.
         */
        template<uint64_t UID, typename ResponseT, typename... RequestTs>
        void RegisterSyncHandler(typename _identity<std::function<ResponseT(RequestTs&...)>>::type func) {
            using CallbackT = std::function<ResponseT(RequestTs&...)>;
            if (protocol_factory_.find(UID) != protocol_factory_.end()) {
                // ID() should be unique, and should not be re-registered
                SIMPLE_ABORT("Duplicate protocol id detected: %d for %s. "
                           "Did you registered the same protocol multiple times?",
                           UID, DecodeUniqueId(UID).c_str());
            }
            CallbackT* fp = new CallbackT(func);
            protocol_factory_[UID] =
                ProtocolFactoryItem(new ProtocolFactory<SyncProtocol<UID, ResponseT, RequestTs...>>(),
                (void*)fp,
                    [=]() {delete fp; });
        }

        /**
         * Registers an asynchronous handler to a protocol with UID
         *
         * \note This is NOT thread-safe. It should not happen in parallel with RPC call or other
         *       register operations.
         *
         * \tparam UID          UID of the protocol.
         * \tparam RequestTs    Type of the request parameters.
         * \param [in,out] func The handler, must have form std::function<ResponseT(RequestTs...)>.
         */
        template<uint64_t UID, typename... RequestTs>
        void RegisterAsyncHandler(typename _identity<std::function<void(RequestTs&...)>>::type func) {
            using CallbackT = std::function<void(RequestTs&...)>;
            if (protocol_factory_.find(UID) != protocol_factory_.end()) {
                // ID() should be unique, and should not be re-registered
                SIMPLE_ABORT("Duplicate protocol id detected: %d for %s. "
                           "Did you registered the same protocol multiple times?",
                           UID, DecodeUniqueId(UID).c_str());
            }
            CallbackT* fp = new CallbackT(func);
            protocol_factory_[UID] =
                ProtocolFactoryItem(new ProtocolFactory<AsyncProtocol<UID, RequestTs...>>(),
                (void*)fp,
                    [=]() {delete fp; });
        }

        /**
         * Registers an asynchronous handler to a protocol with UID, replacing existing handler.
         * 
         * \note This is NOT thread-safe. It should not happen in parallel with RPC call or other
         *       register operations.
         *
         * \tparam UID          UID of the protocol.
         * \tparam RequestTs    Type of the request parameters.
         * \param [in,out] func The handler, must have form std::function<ResponseT(RequestTs...)>.
         */
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
        /**
         * Handles a message by:
         *     1. deserialize the content into requests  
         *     2. calls handler  
         *     3. serialize the response  
         *     4. send the response message back  
         * This is called by RPC worker threads.
         *
         * \param [in,out] msg  The message.
         */
        void HandleMessage(MessagePtr& msg) {
            if (msg->GetStatus() != ErrorCode::SUCCESS) {
                SIMPLE_WARN("RPC get a message of communication failure of machine %s, status=%d",
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
            SIMPLE_LOG("Handle message, seq=%lld, pid=%d, async=%d",
                header.seq_num, header.protocol_id, header.is_async);

            if (header.seq_num < 0) {
                // negative seq number indicates a response to a sync rpc call
                header.seq_num = -header.seq_num;
                ProtocolBase * protocol = sleeping_list_.GetResponsePtr(header.seq_num);
                if (protocol != nullptr) {
                    // null protocol indicates this request already timedout and removed
                    // so we don't need to get the response or signal the thread
                    protocol->UnmarshallResponse(msg->GetStreamBuffer());
                    SIMPLE_ASSERT(msg->GetStreamBuffer().GetSize() == 0,
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
                    SIMPLE_ABORT("Unsupported protocol from %s, protocol ID=%d",
                               EPToString(msg->GetRemoteAddr()).c_str(), header.protocol_id);
                    return;
                }
                ProtocolBase* protocol =
                    protocol_factory_[header.protocol_id].factory->CreateProtocol();
                protocol->UnmarshallRequest(msg->GetStreamBuffer());
                SIMPLE_ASSERT(msg->GetStreamBuffer().GetSize() == 0,
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
                    SIMPLE_LOG("responding to %s with seq=%d, protocol_id=%d\n",
                        EPToString(out_message->GetRemoteAddr()).c_str(), header.seq_num, header.protocol_id);
                    comm_->Send(out_message);
                }
                delete protocol;
            }
        }

        /**
         * Gets a new sequence number for the messages.
         *
         * \return  The new sequence number.
         */
        int64_t GetNewSeqNum() {
            // TODO: use atomic add instead of lock
            LockGuard l(seq_lock_);
            if (seq_num_ >= INT64_MAX - 1)
                seq_num_ = 1;
            return seq_num_++;
        }
    private:
        CommBase<EndPointT> * comm_;
        ProtocolFactories protocol_factory_;
        std::vector<std::thread> worker_threads_;

        int64_t seq_num_;
        std::mutex seq_lock_;

        /** When a thread calls synchronous RPC, it will wait for the response in this list. 
         *  When we receive a response, we will check its sequence id and wake up the corresponding
         *  thread. */
        SleepingList<ProtocolBase> sleeping_list_;
        std::mutex waiting_event_lock_;
        std::unordered_map<EndPointT, std::set<int64_t>> ep_waiting_events_;

        /** Exit flag. If this is set, the worker threads will exit. */
        std::atomic<bool> exit_now_;

        /** have StartServing been called? */
        std::atomic<int> serving_;
    };
};

