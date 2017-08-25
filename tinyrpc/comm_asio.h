#pragma once 

#if USE_ASIO
#include <array>
#include <atomic>
#include "asio/asio.hpp"
#include <exception>
#include <string>
#include <thread>
#include <unordered_map>
#include <mutex>
#include "concurrent_queue.h"
#include "logging.h"
#include "message.h"
#include "serialize.h"
#include "set_thread_name.h"
#include "streambuffer.h"
#include "tinycomm.h"

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "comm_asio"

template<>
class std::hash<asio::ip::tcp::endpoint> {
public:
    size_t operator() (const asio::ip::tcp::endpoint & ep) const {
        uint64_t r = ep.address().to_v4().to_ulong();
        return ((r << 16) | ep.port());
        //return std::hash<std::string>()(ep.address().to_string());
    }
};

namespace tinyrpc {
    typedef asio::ip::tcp::endpoint AsioEP;
    typedef asio::ip::tcp::socket AsioSocket;
    typedef asio::ip::tcp::acceptor AsioAcceptor;
    typedef asio::io_service AsioService;
    typedef asio::strand AsioStrand;
    typedef asio::ip::address AsioAddr;
    typedef std::shared_ptr<std::condition_variable> CvPtr;
    typedef std::lock_guard<std::mutex> LockGuard;
    typedef asio::mutable_buffer AsioMutableBuffer;
    typedef std::shared_ptr<asio::mutable_buffer> AsioBufferPtr;

	template<>
    inline AsioEP MakeEP<AsioEP>(const std::string& host, uint16_t port) {
        return AsioEP(asio::ip::address::from_string(host), port);
    }

    template<>
    class Serializer<AsioEP> {
        static void Serialize(StreamBuffer& buf, const AsioEP& ep) {
            ::tinyrpc::Serialize(buf, ep.address().to_string());
            ::tinyrpc::Serialize(buf, ep.port());
        }

        static void Deserizlie(StreamBuffer& buf, AsioEP& ep) {
            std::string host;
            ::tinyrpc::Deserialize(buf, host);
            uint16_t port;
            ::tinyrpc::Deserialize(buf, port);
            ep = MakeEP<AsioEP>(host, port);
        }
    };

    template<>
    inline const std::string EPToString<AsioEP>(const AsioEP & ep) {
        return ep.address().to_string() + ":" + std::to_string(ep.port());
    }

    inline const std::string ToString(const AsioEP & ep) {
        return EPToString(ep);
    }

    class EPHasher {
    public:
        size_t operator()(const AsioEP & ep) {
            return std::hash<std::string>()(ep.address().to_string());
        }
    };

    class TinyCommAsio : public TinyCommBase<AsioEP>
    {
        const static int NUM_WORKERS = 1;
        const static int RECEIVE_BUFFER_SIZE = 1024;

        struct SocketBuffers {
            SocketBuffers() : sock(nullptr), receive_buffer(RECEIVE_BUFFER_SIZE){}
            ~SocketBuffers() {
                delete sock;
            }
            AsioSocket * sock;
            ResizableBuffer receive_buffer;            
            std::mutex lock;
            AsioEP target;
        private:
            SocketBuffers(const SocketBuffers &);
            SocketBuffers & operator=(const SocketBuffers&);
        };

        typedef std::shared_ptr<SocketBuffers> SocketBuffersPtr;
        typedef std::unordered_map<AsioEP, SocketBuffersPtr> EPSocketMap;
    public:
        /*
        * \brief constructs a asio communicator, listening on a port
        *
        * \param port: if not 0, the communicator will wait for connections on this 
        *              port. Otherwise, communicator will not accept any connections.
        */
        TinyCommAsio(int port = 0)
            : started_(false),
            port_(port),
            exit_now_(false) {
            if (port == 0) return;
            try {
                acceptor_ = std::make_shared<AsioAcceptor>(io_service_);
                acceptor_->open(asio::ip::tcp::v4());
                acceptor_->set_option(asio::socket_base::reuse_address(false));
                acceptor_->bind(AsioEP(asio::ip::tcp::v4(), port));
            }
            catch (std::exception & e) {
                TINY_ABORT("error binding to port %d: %s", port_, e.what());
            }
        }

        virtual ~TinyCommAsio() {
            StopReceiving();
            {
                LockGuard l(sockets_lock_);
                exit_now_ = true;
            }
            io_service_.stop();
            if (acceptor_) acceptor_->close();
            accepting_thread_.join();
            TINY_LOG("asio accepting thread exit");
            for (int i = 0; i < NUM_WORKERS; i++) {
                workers_[i].join();
                TINY_LOG("asio worker thread %d exit", i);
            }
        };

        virtual void StopReceiving() {
            receive_queue_.SignalForKill();
        }

        // start polling for messages
        virtual void Start() override {
            if (started_) {
                return;
            }
            started_ = true;
            accepting_thread_ = std::thread([this](){AcceptingThreadFunc(); });
            workers_.resize(NUM_WORKERS);
            for (int i = 0; i < NUM_WORKERS; i++) {
                workers_[i] = std::thread([this, i]() {
                    SetThreadName("asio worker", i);
                    try {
                        asio::io_service::work work(io_service_);
                        io_service_.run();
                    }
                    catch (std::exception & e) {
                        TINY_WARN("asio worker %d hit an exception and has to exit: %s", e.what());
                        return;
                    }
                });
            }
        };

        // send/receive
        virtual CommErrors Send(const MessagePtr & msg) override {
            // pad a uint64_t size at the head of the buffer
            uint64_t size = msg->GetStreamBuffer().GetSize() + sizeof(uint64_t);
            msg->GetStreamBuffer().WriteHead(size);
            SocketBuffersPtr socket;
            try {
                socket = GetSocket(msg->GetRemoteAddr());
                if (socket == nullptr) {
                    TINY_ASSERT(exit_now_, "socket is null, but exit_now_ is not");
                    return CommErrors::SEND_ERROR;
                }
                LockGuard sl(socket->lock);
                asio::write(*(socket->sock),
                    asio::buffer(msg->GetStreamBuffer().GetBuf(), msg->GetStreamBuffer().GetSize()));
            }
            catch (std::exception & e) {
                TINY_WARN("communication error occurred: %s", e.what());
                asio::error_code err;
                if (socket) {
                    HandleFailureWithEc(socket, err);
                }                
                return CommErrors::SEND_ERROR;
            }            
            return CommErrors::SUCCESS;
        };

        virtual MessagePtr Recv() override {
            MessagePtr msg = nullptr;
            receive_queue_.Pop(msg);
            return msg;
        };

    private:
        void AcceptingThreadFunc() {
            SetThreadName("asio accept thread");
            // if port==0 is specified, acceptor_ will be null
            if (!acceptor_) return;
            try {
                acceptor_->listen();
                TINY_LOG("listening on %d", acceptor_->local_endpoint().port());
                while (true) {
                    try {
                        AsioSocket* sock = new AsioSocket(io_service_);
                        acceptor_->accept(*sock);
                        if (exit_now_) {
                            return;
                        }
                        const AsioEP & remote = sock->remote_endpoint();
                        TINY_LOG("new client connected: %s", EPToString(remote).c_str());
                        LockGuard l(sockets_lock_);
                        if (exit_now_) {
                            return;
                        }
                        SocketBuffersPtr & socket = sockets_[remote];
                        if (socket == nullptr) {
                            socket = SocketBuffersPtr(new SocketBuffers());
                        }
                        LockGuard(socket->lock);
                        TINY_ASSERT(socket->sock == nullptr, "this socket seems to have connected: %s", EPToString(remote).c_str());
                        if (socket->sock == nullptr) {
                            socket->sock = sock;
                        }
                        else {
                            delete sock;
                        }
                        socket->target = remote;
                        PostAsyncReadNoLock(socket);
                    }
                    catch (...) {
                        if (exit_now_) {
                            return;
                        }
                        TINY_ABORT("something wrong has happened");
                    }
                }
            }
            catch (std::exception & e) {
                if (exit_now_) {
                    return;
                }
                TINY_ABORT("error occurred: %s", e.what());
            }
        }

        inline void PostAsyncReadNoLock(const SocketBuffersPtr & socket) {
            // ASSUMING socket.lock is held
            try {
                void * buf = socket->receive_buffer.GetWritableBuf();
                size_t size = socket->receive_buffer.GetWritableSize();
                TINY_ASSERT(buf != nullptr && size != 0, "no buf space left, buf=%p, size=%llu", buf, size);
                socket->sock->async_read_some(asio::buffer(buf, size),
                    [this, socket](const asio::error_code& ec, std::size_t bytes_transferred)
                {
                    HandleRead(socket, ec, bytes_transferred);
                });
            }
            catch (std::exception & e) {
                if (exit_now_)
                {
                    return;
                }
                TINY_ABORT("hit an exception: %s", e.what());
            }
        }

        void RecvLongMessage(SocketBuffersPtr socket, size_t package_size) {
            try {
                LockGuard sl(socket->lock);
                while (socket->receive_buffer.GetReceivedBytes() < package_size) {
                    void * buf = socket->receive_buffer.GetWritableBuf();
                    size_t writable_size = socket->receive_buffer.GetWritableSize();
                    size_t bytes_to_read = package_size - socket->receive_buffer.GetReceivedBytes();
                    TINY_ASSERT(buf != nullptr && writable_size >= bytes_to_read,
                        "no buf space left, buf=%p, size=%llu", buf, writable_size);
                    size_t bytes = socket->sock->receive(asio::buffer(buf, writable_size));
                    socket->receive_buffer.MarkReceiveBytes(bytes);
                }
                SealMessageNoLock(socket, package_size);
                PostAsyncReadNoLock(socket);
            }
            catch (std::exception & e) {
                if (exit_now_) {
                    return;
                }
                TINY_WARN("read error from %s:%d, trying to handle failure...",
                    socket->target.address().to_string().c_str(), socket->target.port());
                HandleFailure(socket, e.what());
            }
        }

        void SealMessageNoLock(SocketBuffersPtr socket, size_t package_size) {
            TINY_LOG("A complete packet is received, size=%lld", package_size);
            // have received the whole message, pack it into MessagePtr and start receiving next one
            MessagePtr message(new MessageType);
            message->SetRemoteAddr(socket->target);
            message->GetStreamBuffer().SetBuf(
                (char*)socket->receive_buffer.RenewBuf(RECEIVE_BUFFER_SIZE), package_size);
            uint64_t size;
            // remove the head uint64_t before passing it to RPC
            message->GetStreamBuffer().Read(&size, sizeof(size));
            message->SetStatus(TinyErrorCode::SUCCESS);
            receive_queue_.Push(message);
        }

        void HandleRead(SocketBuffersPtr socket, const asio::error_code & ec, std::size_t bytes_transferred) {
            if (exit_now_)
                return;
            if (ec) {
                TINY_WARN("read error from %s:%d, trying to handle failture...", 
                    socket->target.address().to_string().c_str(), socket->target.port());
                HandleFailureWithEc(socket, ec);
            }
            else {
                bool need_post_read = true;
                LockGuard sl(socket->lock);
                const AsioEP & remote = socket->sock->remote_endpoint();
                TINY_LOG("received %llu bytes from socket", bytes_transferred);
                socket->receive_buffer.MarkReceiveBytes(bytes_transferred);
                size_t bytes_received_total = socket->receive_buffer.GetReceivedBytes();
                // packet will arrive with the uint64_t size at the head
                if (bytes_received_total >= sizeof(size_t)) {
                    uint64_t package_size = *(uint64_t*)socket->receive_buffer.GetBuf();
                    TINY_ASSERT(package_size < (size_t)16 * 1024 * 1024 * 1024, 
                        "alarmingly large package_size: %lld", package_size);
                    if (bytes_received_total < package_size) {
                        if (socket->receive_buffer.Size() < package_size) {
                            socket->receive_buffer.Resize(package_size);
                        }
                        if (package_size >= 10 * 1024 * 1024) {
                        //if (0) {
                            // spawn a new thread to do the long read
                            std::thread t([this, socket, package_size]() {
                                RecvLongMessage(socket, package_size);
                            });
                            t.detach();
                            // the receiving thread will post async read, so we
                            // should not do it in this thread
                            need_post_read = false;
                        }
                    }
                    else {
                        if (bytes_received_total == package_size) {
                            SealMessageNoLock(socket, package_size);
                        }
                        else {
                            // it is possible that we have received multiple packages, 
                            // in which case bytes_received_total > package_size
                            char * received_buf = (char*)socket->receive_buffer.GetBuf();
                            uint64_t package_start = 0;
                            uint64_t bytes_left = bytes_received_total;
                            while (true) {
                                package_size = *(uint64_t*)(received_buf + package_start);
                                TINY_ASSERT(package_start < bytes_received_total, "something is really wrong");
                                TINY_LOG("A complete packet is received, size=%lld", package_size);
                                char * package_buf = new char[package_size];
                                memcpy(package_buf, received_buf + package_start, package_size);
                                MessagePtr message(new MessageType);
                                message->SetRemoteAddr(socket->target);
                                message->GetStreamBuffer().SetBuf(package_buf, package_size);
                                uint64_t size;
                                // remove the head uint64_t before passing it to RPC
                                message->GetStreamBuffer().Read(&size, sizeof(size));
                                message->SetStatus(TinyErrorCode::SUCCESS);
                                receive_queue_.Push(message);
                                package_start += package_size;
                                bytes_left -= package_size;
                                if (bytes_left < sizeof(uint64_t)
                                    || bytes_left < *(uint64_t*)(received_buf + package_start)) {
                                    break;
                                }                                
                            }
                            // ok, now we have something left in the buffer, but not a whole package
                            // we should move the content to the front of the buffer and continue
                            // receiving messages
                            socket->receive_buffer.Compact(package_start);
                        }
                    }
                }
                // post a new read request if required
                if (need_post_read) PostAsyncReadNoLock(socket);
            }
        }

        void HandleFailureWithEc(SocketBuffersPtr socket, const asio::error_code& ec) {
            HandleFailure(socket, ec.message());
        }

        void HandleFailure(SocketBuffersPtr socket, const std::string& msg) {
            TINY_WARN("a network failure occurred, error=%s", msg.c_str());
            LockGuard l(sockets_lock_);
            LockGuard sl(socket->lock);
            if (exit_now_)
                return;
            if (socket->sock != nullptr) {
                // notify failure by sending a special message
                MessagePtr message(new MessageType);
                message->SetStatus(TinyErrorCode::SERVER_FAIL);
                message->SetRemoteAddr(socket->target);
                receive_queue_.Push(message);
            }
            auto it = sockets_.find(socket->target);
            if (it != sockets_.end() && it->second == socket) {
                sockets_.erase(it);
            }
            TINY_LOG("socket closed, now number of sockets becomes %llu", sockets_.size());
        }

        SocketBuffersPtr GetSocket(const AsioEP & remote) {
            LockGuard l(sockets_lock_);
            if (exit_now_) return nullptr;
            SocketBuffersPtr & socket = sockets_[remote];
            if (socket == nullptr) {
                socket = SocketBuffersPtr(new SocketBuffers());
                socket->target = remote;
            }
            LockGuard sl(socket->lock);
            if (socket->sock == nullptr) {
                AsioSocket * sock = new AsioSocket(io_service_);
                try {
                    sock->connect(remote);
                }
                catch (std::exception & e) {
                    if (exit_now_) {
                        return nullptr;
                    }
                    TINY_WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string().c_str(), remote.port(), e.what());
                    throw e;
                }
                TINY_LOG("connected to server: %s:%d", remote.address().to_string().c_str(), remote.port());
                // when we have a null socket, the sending buffer and receiving buffer must be empty
                TINY_ASSERT(socket->receive_buffer.GetReceivedBytes() == 0,
                    "unexpected non-empty receive buffer");
                // now, post a async read
                socket->receive_buffer.Resize(RECEIVE_BUFFER_SIZE);
                socket->sock = sock;
                PostAsyncReadNoLock(socket);
            }
            return socket;
        }

        bool started_;
        ConcurrentQueue<MessagePtr> receive_queue_;

        AsioService io_service_;
        std::shared_ptr<AsioAcceptor> acceptor_;
        std::mutex sockets_lock_;
        EPSocketMap sockets_;
        uint16_t port_;

        std::thread accepting_thread_;
        std::vector<std::thread> workers_;
        std::atomic<bool> exit_now_;
    };
};
#endif
