#pragma once 
#include <array>
#include "asio/asio.hpp"
#include <exception>
#include <string>
#include <thread>
#include <unordered_map>
#include <mutex>
#include "concurrentqueue.h"
#include "logging.h"
#include "streambuffer.h"
#include "tinycomm.h"
#include "message.h"

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "CommAsio"

template<>
class std::hash<asio::ip::tcp::endpoint>
{
public:
    size_t operator() (const asio::ip::tcp::endpoint & ep) const
    {
        return std::hash<std::string>()(ep.address().to_string());
    }
};

namespace TinyRPC
{
    typedef asio::ip::tcp::endpoint asioEP;
    typedef asio::ip::tcp::socket asioSocket;
    typedef asio::ip::tcp::acceptor asioAcceptor;
    typedef asio::io_service asioService;
    typedef asio::strand asioStrand;
    typedef asio::ip::address asioAddr;
    typedef std::shared_ptr<std::condition_variable> cvPtr;
    typedef std::lock_guard<std::mutex> LockGuard;
    typedef asio::mutable_buffer asioMutableBuffer;
    typedef std::shared_ptr<asio::mutable_buffer> asioBufferPtr;

    template<>
    inline const std::string EPToString<asioEP>(const asioEP & ep)
    {
        return ep.address().to_string() + ":" + std::to_string(ep.port());
    }

    class EPHasher
    {
    public:
        size_t operator()(const asioEP & ep)
        {
            return std::hash<std::string>()(ep.address().to_string());
        }
    };

    class TinyCommAsio : public TinyCommBase<asioEP>
    {
        const static int NUM_WORKERS = 2;
        const static int RECEIVE_BUFFER_SIZE = 1024;

        struct SocketBuffers
        {
            SocketBuffers() : sock(nullptr), receive_buffer(RECEIVE_BUFFER_SIZE){}
            ~SocketBuffers()
            {
                delete sock;
            }
            asioSocket * sock;
            ResizableBuffer receive_buffer;            
            std::mutex lock;
            asioEP target;
        private:
            SocketBuffers(const SocketBuffers &);
            SocketBuffers & operator=(const SocketBuffers&);
        };

        typedef std::shared_ptr<SocketBuffers> SocketBuffersPtr;
        typedef std::unordered_map<asioEP, SocketBuffersPtr> EPSocketMap;
    public:
        TinyCommAsio(int port)
            : started_(false),
            port_(port),
            exit_now_(false)
        {
            try
            {
                acceptor_ = std::make_shared<asioAcceptor>(io_service_);
                acceptor_->open(asio::ip::tcp::v4());
                acceptor_->set_option(asio::socket_base::reuse_address(false));
                acceptor_->bind(asioEP(asio::ip::tcp::v4(), port));
            }
            catch (std::exception & e)
            {
                ABORT("error binding to port %d: %s", port_, e.what());
            }
        }

        virtual ~TinyCommAsio()
        {
            wake_threads_for_exit();
            {
                LockGuard l(sockets_lock_);
                exit_now_ = true;
                //acceptor_->close();
                //for (auto & s : sockets_)
                //{
                //    LockGuard ls(s.second->lock);
                //    s.second->sock->close();
                //    // sock will be deleted when SocketBuffers get destroyed
                //}
            }
            io_service_.stop();
            acceptor_->close();
            accepting_thread_.join();
            LOG("asio accepting thread exit");
            for (int i = 0; i < NUM_WORKERS; i++)
            {
                workers_[i].join();
                LOG("asio worker thread %d exit", i);
            }
        };

        virtual void wake_threads_for_exit()
        {
            receive_queue_.signalForKill();
        }

        // start polling for messages
        virtual void start() override 
        {
            if (started_)
            {
                return;
            }
            started_ = true;
            accepting_thread_ = std::thread([this](){accepting_thread_func(); });
            workers_.resize(NUM_WORKERS);
            for (int i = 0; i < NUM_WORKERS; i++)
            {
                workers_[i] = std::thread([this, i]()
                {
                    SetThreadName("asio worker", i);
                    try
                    {
                        asio::io_service::work work(io_service_);
                        io_service_.run();
                    }
                    catch (std::exception & e)
                    {
                        WARN("asio worker %d hit an exception and has to exit: %s", e.what());
                        return;
                    }
                });
            }
        };

        // send/receive
        virtual CommErrors send(const MessagePtr & msg) override
        {
            // pad a uint64_t size at the head of the buffer
            uint64_t size = msg->get_stream_buffer().get_size() + sizeof(uint64_t);
            msg->get_stream_buffer().write_head(size);
            SocketBuffersPtr socket;
            try
            {
                socket = get_socket(msg->get_remote_addr());
                if (socket == nullptr)
                {
                    ASSERT(exit_now_, "socket is null, but exit_now_ is not");
                    return CommErrors::SEND_ERROR;
                }
                LockGuard sl(socket->lock);
                asio::write(*(socket->sock),
                    asio::buffer(msg->get_stream_buffer().get_buf(), msg->get_stream_buffer().get_size()));
            }
            catch (std::exception & e)
            {
                WARN("communication error occurred: %s", e.what());
                asio::error_code err;
                if (socket)
                {
                    handle_failure(socket, err);
                }                
                return CommErrors::SEND_ERROR;
            }            
            return CommErrors::SUCCESS;
        };

        virtual MessagePtr recv() override
        {
            MessagePtr msg = nullptr;
            receive_queue_.pop(msg);
            return msg;
        };

    private:
        void accepting_thread_func()
        {
            SetThreadName("asio accept thread");  
            try
            {
                acceptor_->listen();
                LOG("listening on %d", acceptor_->local_endpoint().port());
                while (true)
                {
                    try
                    {
                        asioSocket* sock = new asioSocket(io_service_);
                        acceptor_->accept(*sock);
                        if (exit_now_)
                        {
                            return;
                        }
                        asioEP & remote = sock->remote_endpoint();
                        LockGuard l(sockets_lock_);
                        if (exit_now_)
                        {
                            return;
                        }
                        SocketBuffersPtr & socket = sockets_[remote];
                        if (socket == nullptr)
                        {
                            socket = SocketBuffersPtr(new SocketBuffers());
                        }
                        LockGuard(socket->lock);
                        ASSERT(socket->sock == nullptr, "this socket seems to have connected: %s", EPToString(remote).c_str());
                        if (socket->sock == nullptr)
                        {
                            socket->sock = sock;
                        }
                        else
                        {
                            delete sock;
                        }
                        socket->target = remote;
                        post_async_read(socket);
                    }
                    catch (...)
                    {
                        if (exit_now_)
                        {
                            return;
                        }
                        ABORT("something wrong has happened");
                    }
                }
            }
            catch (std::exception & e)
            {
                if (exit_now_)
                {
                    return;
                }
                ABORT("error occurred: %s", e.what());
            }
        }

        inline void post_async_read(const SocketBuffersPtr & socket)
        {
            // ASSUMING socket.lock is held
            try
            {
                void * buf = socket->receive_buffer.get_writable_buf();
                size_t size = socket->receive_buffer.get_writable_size();
                ASSERT(buf != nullptr && size != 0, "no buf space left, buf=%p, size=%llu", buf, size);
                socket->sock->async_read_some(asio::buffer(buf, size),
                    [this, socket](const asio::error_code& ec, std::size_t bytes_transferred)
                {
                    handle_read(socket, ec, bytes_transferred);
                });
            }
            catch (std::exception & e)
            {
                if (exit_now_)
                {
                    return;
                }
                ABORT("hit an exception: %s", e.what());
            }
        }

        void handle_read(SocketBuffersPtr socket, const asio::error_code & ec, std::size_t bytes_transferred)
        {
            if (exit_now_)
                return;
            if (ec)
            {
                WARN("read error from %s:%d, try to handle failture", 
                    socket->target.address().to_string().c_str(), socket->target.port());
                handle_failure(socket, ec);
            }
            else
            {
                LockGuard sl(socket->lock);
                asioEP & remote = socket->sock->remote_endpoint();
                LOG("received %llu bytes from socket", bytes_transferred);
                socket->receive_buffer.mark_receive_bytes(bytes_transferred);
                size_t bytes_received_total = socket->receive_buffer.get_received_bytes();
                // packet will arrive with the uint64_t size at the head
                if (bytes_received_total >= sizeof(size_t))
                {
                    uint64_t package_size = *(uint64_t*)socket->receive_buffer.get_buf();
                    ASSERT(package_size < (size_t)16 * 1024 * 1024 * 1024, "alarmingly large package_size: %lld", package_size);
                    if (bytes_received_total < package_size)
                    {
                        if (socket->receive_buffer.size() < package_size)
                        {
                            socket->receive_buffer.resize(package_size);
                        }
                    }
                    else
                    {
                        if (bytes_received_total == package_size)
                        {
                            LOG("A complete packet is received, size=%lld", package_size);
                            // have received the whole message, pack it into MessagePtr and start receiving next one
                            MessagePtr message(new MessageType);
                            message->set_remote_addr(socket->target);
                            message->get_stream_buffer().set_buf(
                                (char*)socket->receive_buffer.renew_buf(RECEIVE_BUFFER_SIZE), package_size);
                            uint64_t size;
                            // remove the head uint64_t before passing it to RPC
                            message->get_stream_buffer().read(size);
                            receive_queue_.push(message);
                        }
                        else
                        {
                            // it is possible that we have received multiple packages, 
                            // in which case bytes_received_total > package_size
                            char * received_buf = (char*)socket->receive_buffer.get_buf();
                            uint64_t package_start = 0;
                            uint64_t bytes_left = bytes_received_total;
                            while (true)
                            {
                                package_size = *(uint64_t*)(received_buf + package_start);
                                ASSERT(package_start < bytes_received_total, "something is really wrong");
                                LOG("A complete packet is received, size=%lld", package_size);
                                char * package_buf = new char[package_size];
                                memcpy(package_buf, received_buf + package_start, package_size);
                                MessagePtr message(new MessageType);
                                message->set_remote_addr(socket->target);
                                message->get_stream_buffer().set_buf(package_buf, package_size);
                                uint64_t size;
                                // remove the head uint64_t before passing it to RPC
                                message->get_stream_buffer().read(size);
                                receive_queue_.push(message);
                                package_start += package_size;
                                bytes_left -= package_size;
                                if (bytes_left < sizeof(uint64_t)
                                    || bytes_left < *(uint64_t*)(received_buf + package_start))
                                {
                                    break;
                                }                                
                            }
                            // ok, now we have something left in the buffer, but not a whole package
                            // we should move the content to the front of the buffer and continue
                            // receiving messages
                            socket->receive_buffer.compact(package_start);
                        }
                    }
                }
                // no matter what happended, we should post a new read request
                post_async_read(socket);
            }
        }

        void handle_failure(SocketBuffersPtr socket, const asio::error_code& ec)
        {
            WARN("a network failure occurred, ec=%s", ec.message().c_str());
            LockGuard l(sockets_lock_);
            LockGuard sl(socket->lock);
            if (exit_now_)
                return;
            if (socket->sock != nullptr)
            {
                // notify failure by sending a special message
                MessagePtr message(new MessageType);
                message->set_status(TinyErrorCode::SERVER_FAIL);
                message->set_remote_addr(socket->target);
                receive_queue_.push(message);
            }
            auto it = sockets_.find(socket->target);
            if (it != sockets_.end() && it->second == socket)
            {
                sockets_.erase(it);
            }
        }

        SocketBuffersPtr get_socket(const asioEP & remote)
        {
            LockGuard l(sockets_lock_);
            if (exit_now_)
                return nullptr;
            SocketBuffersPtr & socket = sockets_[remote];
            if (socket == nullptr)
            {
                socket = SocketBuffersPtr(new SocketBuffers());
                socket->target = remote;
            }
            LockGuard sl(socket->lock);
            if (socket->sock == nullptr)
            {
                asioSocket * sock = new asioSocket(io_service_);
                try
                {
                    sock->connect(remote);
                }
                catch (std::exception & e)
                {
                    if (exit_now_)
                    {
                        return nullptr;
                    }
                    WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string().c_str(), remote.port(), e.what());
                    throw e;
                }
                LOG("connected to server: %s:%d", remote.address().to_string().c_str(), remote.port());
                // when we have a null socket, the sending buffer and receiving buffer must be empty
                ASSERT(socket->receive_buffer.get_received_bytes() == 0,
                    "unexpected non-empty receive buffer");
                // now, post a async read
                socket->receive_buffer.resize(RECEIVE_BUFFER_SIZE);
                socket->sock = sock;
                post_async_read(socket);
            }
            return socket;
        }

        bool started_;
        ConcurrentQueue<MessagePtr> receive_queue_;

        asioService io_service_;
        std::shared_ptr<asioAcceptor> acceptor_;
        std::mutex sockets_lock_;
        EPSocketMap sockets_;
        uint16_t port_;

        std::thread accepting_thread_;
        std::vector<std::thread> workers_;
        std::atomic<bool> exit_now_;
    };
};
