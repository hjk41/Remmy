#pragma once

//#ifdef USE_ZMQ
#if 1
#include <exception>
#include <mutex>
#include <string>
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
        uint32_t ip_binary_;
        uint64_t hash_;
    public:
        ZmqEP() {}
        ZmqEP(const std::string& ip, uint16_t port) 
            : ip_string_(ip),
              port_(port) {
            ip_binary_ = 0;
            size_t p = 0;
            for (int i = 0; i < 4; i++) {
                uint8_t d = atoi(ip_string_.c_str() + p);
                ip_binary_ = ((ip_binary_ << 8) | d);
                if (i < 3) {
                    p = ip_string_.find('.', p);
                    if (p == ip_string_.npos) {
                        TINY_ABORT("Error parsing ip address %s", ip_string_.c_str());
                    }
                    p++;
                }
            }
            hash_ = (((uint64_t)ip_binary_ << 16) | port_);
        }

        ZmqEP(uint32_t ip_binary, uint16_t port) {
            port_ = port;
            ip_binary_ = ip_binary;
            hash_ = (((uint64_t)ip_binary_ << 16) | port_);
        }

        std::string ToString() const {
            if (ip_string_.empty()) {
                uint32_t binary = ip_binary_;
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

        uint32_t IpBinary() const {
            return ip_binary_;
        }

        uint16_t Port() const {
            return port_;
        }

        void Serialize(StreamBuffer& buf) const {
            buf.Write(&ip_binary_, sizeof(ip_binary_));
            buf.Write(&port_, sizeof(port_));
        }

        void Deserialize(StreamBuffer& buf) {
            buf.Read(&ip_binary_, sizeof(ip_binary_));
            buf.Read(&port_, sizeof(port_));
        }
    };

    struct ZmqEPHasher {
        uint64_t operator()(const ZmqEP& ep) {
            return ep.Hash();
        }
    };

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
            std::unordered_map<ZmqEP, zmq::socket_t, ZmqEPHasher> sockets_;
            std::vector<zmq::context_t> contexts_;
        public:
            zmq::socket_t& GetSocket(const ZmqEP& ep) {
                auto it = sockets_.find(ep);
                if (it == sockets_.end()) {
                    // create a new socket
                    if (sockets_.size() % FD_PER_CONTEXT == 0) {
                        contexts_.emplace_back();
                    }
                    zmq::socket_t sock(contexts_.back(), ZMQ_DEALER);
                    sock.connect(ep.ToString());
                    it = sockets_.emplace_hint(it, ep, sock);
                }
                return it->second;
            }
        };

    public:
        TinyCommZmq(const std::string& ip, int port = 0) 
            : my_ep_(ip, port) {
        }

        virtual ~TinyCommZmq() {}

        virtual void Stop() override {}

        virtual void Start() override {}

        virtual CommErrors Send(const MessagePtr& msg) override {
        }

        virtual MessagePtr Recv() override {
        }
    private:
        ZmqEP my_ep_;
        std::thread reader_;
        std::thread sender_;
        ConcurrentQueue<MessagePtr> outbox_;
        ConcurrentQueue<MessagePtr> inbox_;
    };
}

#endif