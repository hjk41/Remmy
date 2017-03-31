#pragma once

//#ifdef USE_ZMQ
#if 1
#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "zmq.hpp"

#include "concurrent_queue.h"
#include "logging.h"
#include "message.h"
#include "serialize.h"
#include "set_thread_name.h"
#include "streambuffer.h"
#include "tinycomm.h"

namespace tinyrpc {
    class ZmqEP {
        mutable std::string ip_string_;
        uint16_t port_;
        uint64_t hash_;
    public:
        ZmqEP() {}
        ZmqEP(const std::string& ip, uint16_t port)
            : ip_string_(ip),
            port_(port) {
            uint32_t ip_binary = 0;
            size_t p = 0;
            for (int i = 0; i < 4; i++) {
                uint8_t d = atoi(ip_string_.c_str() + p);
                ip_binary = ((ip_binary << 8) | d);
                if (i < 3) {
                    p = ip_string_.find('.', p);
                    if (p == ip_string_.npos) {
                        TINY_ABORT("Error parsing ip address %s", ip_string_.c_str());
                    }
                    p++;
                }
            }
            hash_ = (((uint64_t)ip_binary << 16) | port_);
        }

        ZmqEP(uint64_t hash) {
            hash_ = hash;
            port_ = hash_ & 0xffff;
        }

        void SetPort(uint16_t port) {
            port_ = port;
            hash_ = ((hash_ >> 16) << 16) | port;
        }

        bool operator==(const ZmqEP& rhs) const {
            return hash_ == rhs.hash_;
        }

        std::string ToString() const {
            if (ip_string_.empty()) {
                uint32_t binary = (uint32_t)(hash_ >> 16);
                for (int i = 0; i < 4; i++) {
                    uint8_t d = (binary & 0xff000000) >> 24;
                    ip_string_ += std::to_string(d);
                    if (i != 3) ip_string_ += ".";
                    binary = (binary << 8);
                }
            }
            return std::string("tcp://") + ip_string_ + ":" + std::to_string(port_);
        }

        uint64_t Hash() const {
            return hash_;
        }

        uint16_t Port() const {
            return port_;
        }

        void Serialize(StreamBuffer& buf) const {
            buf.Write(&hash_, sizeof(hash_));
        }

        void Deserialize(StreamBuffer& buf) {
            buf.Read(&hash_, sizeof(hash_));
            port_ = hash_ & 0xffff;
        }
    };

    template<>
    inline const std::string EPToString<ZmqEP>(const ZmqEP& ep) {
        return ep.ToString();
    }

	template<>
	inline ZmqEP MakeEP<ZmqEP>(const std::string& host, uint16_t port) {
		return ZmqEP(host, port);
	}
}

namespace std {
template<>
struct hash<tinyrpc::ZmqEP> {
    uint64_t operator()(const tinyrpc::ZmqEP &ep) const {
        return ep.Hash();
    }
};
}

namespace tinyrpc{
    class TinyCommZmq : public TinyCommBase<ZmqEP> {
        /*
        * \brief A pool of sockets, only for single thread usage
        */
        class ConnectionPool {
            /* Maximum number of open connections per context
             * This is to work around the "too many opened files" problem
             * when we maintain too many open connections. ZMQ can keep
             * at most 1024 open connections per context. So if we exceeds
             * this amount, we should create a new context.
            */
            const static size_t FD_PER_CONTEXT = 1000;
            std::unordered_map<ZmqEP, zmq::socket_t> sockets_;
            std::vector<zmq::context_t> contexts_;
        public:
            virtual ~ConnectionPool() {
                sockets_.clear();
                contexts_.clear();
            }

            zmq::socket_t& GetSocket(const ZmqEP& ep) {
                auto it = sockets_.find(ep);
                if (it == sockets_.end()) {
                    // create a new socket
                    size_t context_id = (sockets_.size() + FD_PER_CONTEXT - 1) / FD_PER_CONTEXT;
                    if (contexts_.size() <= context_id) {
                        contexts_.emplace_back();
                    }
                    zmq::socket_t sock(contexts_[context_id], ZMQ_DEALER);
                    sock.connect(ep.ToString());
                    it = sockets_.emplace_hint(it, (const ZmqEP)ep, std::move(sock));
                }
                return it->second;
            }
        };

    public:
        TinyCommZmq(const std::string& ip, int port = 0, int queue_size = 10)
            : my_ep_(ip, port),
            context_(),
            in_socket_(context_, ZMQ_DEALER),
            inbox_(queue_size),
            outbox_(queue_size) {
            kill_ = false;

            in_socket_.setsockopt(ZMQ_RCVHWM, 10);
            in_socket_.bind(my_ep_.ToString());

            char port_str[128]; //make this sufficiently large.
            //otherwise an error will be thrown because of invalid argument.
            size_t size = sizeof(port_str);
            in_socket_.getsockopt(ZMQ_LAST_ENDPOINT, &port_str, &size);
//            printf("socket is bound at port %s\n",port_str);
            std::string s(port_str);
            std::string sport = s.substr(s.find_last_of(':') + 1);
//            printf("socket is bound at port %s\n",sport.c_str());
//            printf("socket is bound at port %d\n",atoi(sport.c_str()));
            my_ep_.SetPort(atoi(sport.c_str()));
            printf("socket is bound at port %d\n",my_ep_.Port());
        }

        virtual ~TinyCommZmq() {
            kill_ = true;
            inbox_.SignalForKill();
            outbox_.SignalForKill();
            receiver_.join();
            sender_.join();
        }

        ZmqEP EP() const {
            return my_ep_;
        }

        virtual void StopReceiving() override {
            inbox_.SignalForKill();
        }

        virtual void Start() override {

            sender_ = std::thread([this]() {
                SenderThread();
            });

            receiver_ = std::thread(&TinyCommZmq::ReceiverThread, this);
        }

        virtual CommErrors Send(const MessagePtr& msg) override {
            bool r = outbox_.Push(msg);
            if (!r) return CommErrors::CONNECTION_ABORTED;
            return CommErrors::SUCCESS;
        }

        virtual MessagePtr Recv() override {
            MessagePtr msg;
            bool r = inbox_.Pop(msg);
            if (!r) {
                TINY_WARN("Recv() killed when waiting for new messages");
            }
            return msg;
        }
    private:
        void SenderThread() {
            SetThreadName("ZMQ sender thread");
            ConnectionPool out_sockets_;
            MessagePtr msg;
            while (outbox_.Pop(msg)) {
                TINY_LOG("Sending message of size %llu", msg->GetStreamBuffer().GetSize());
                // send a message through a zmq socket
                auto& buf = msg->GetStreamBuffer();
                // prepend my address
                buf.WriteHead(my_ep_.Hash());
                // prepend total size of message, including the size itself
                buf.WriteHead((uint64_t)buf.GetSize() + sizeof(size_t));
                void* mem;
                size_t size;
                buf.DetachBuf(&mem, &size);
                zmq::socket_t& sock = out_sockets_.GetSocket(msg->GetRemoteAddr());
                zmq::message_t zmsg(mem, size, StreamBuffer::FreeDetachedBuf);
                TINY_LOG("sending message of size %llu", zmsg.size());
                sock.send(zmsg);
            }
        }

        void ReceiverThread() {
            SetThreadName("ZMQ receiver thread");
            zmq_pollitem_t items[] = {
                { in_socket_, 0, ZMQ_POLLIN, 0 }
            };
            while (!kill_) {
                zmq_poll(items, 1, 1000);
                if (items[0].revents & ZMQ_POLLIN) {
                    MessagePtr msg(new MessageType);
                    msg->SetStatus(TinyErrorCode::SUCCESS);
                    // TODO: avoid memory copy
                    zmq::message_t zmsg;
                    in_socket_.recv(&zmsg);
                    TINY_LOG("received message of size %llu", zmsg.size());

                    const char* data = (const char*)zmsg.data();
                    uint64_t psize = *(uint64_t*)data;
                    data += sizeof(psize);
                    TINY_ASSERT(psize == zmsg.size(),
                        "Unexpected package size: expected %llu, got %llu",
                        psize,
                        zmsg.size());
                    uint64_t ep_hash = *(uint64_t*)data;
                    data += sizeof(ep_hash);
                    msg->SetRemoteAddr(ZmqEP(ep_hash));
                    size_t data_size = zmsg.size() - sizeof(psize) - sizeof(ep_hash);
                    char* buf = (char*)malloc(data_size);
                    memcpy(buf, data, data_size);
                    msg->GetStreamBuffer().SetBuf(buf, data_size);
                    bool r = inbox_.Push(msg);
                    if (!r) {
                        TINY_WARN("RecvMsg() interruptted when trying to push message");
                        break;
                    }
                }
            }
        }

        ZmqEP my_ep_;
        zmq::context_t context_;
        zmq::socket_t in_socket_;
        std::thread receiver_;
        std::thread sender_;
        std::atomic<bool> kill_;
        ConcurrentQueue<MessagePtr> outbox_;
        ConcurrentQueue<MessagePtr> inbox_;
    };
}

#endif
