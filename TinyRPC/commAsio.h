#pragma once 
#include <array>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
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

namespace TinyRPC
{
	typedef boost::asio::ip::tcp::endpoint asioEP;
	typedef boost::asio::ip::tcp::socket asioSocket;
	typedef boost::asio::ip::tcp::acceptor asioAcceptor;
	typedef boost::asio::io_service asioService;
	typedef boost::asio::strand asioStrand;
	typedef boost::asio::ip::address asioAddr;
    typedef std::shared_ptr<std::condition_variable> cvPtr;
    typedef std::lock_guard<std::mutex> LockGuard;
    typedef boost::asio::mutable_buffer asioMutableBuffer;
    typedef std::shared_ptr<boost::asio::mutable_buffer> asioBufferPtr;

    template<>
    const std::string EPToString<asioEP>(const asioEP & ep)
    {
        return ep.address().to_string() + std::to_string(ep.port());
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
        const static int NUM_WORKERS = 8;
        const static int NUM_SENDING_THREADS = 2;
        const static int RECEIVE_BUFFER_SIZE = 1024;

        struct SocketBuffers
        {
            SocketBuffers() : sock(nullptr), receive_buffer(RECEIVE_BUFFER_SIZE){}
            ~SocketBuffers(){ delete sock; }
            asioSocket * sock;
            ResizableBuffer receive_buffer;            
            std::mutex lock;
            asioEP target;
        private:
            SocketBuffers(const SocketBuffers &);
            SocketBuffers & operator=(const SocketBuffers&);
        };

        typedef std::shared_ptr<SocketBuffers> SocketBuffersPtr;
        typedef std::unordered_map<asioEP, SocketBuffersPtr, EPHasher> EPSocketMap;
    public:
        TinyCommAsio(int port)
            : acceptor_(io_service_, asioEP(boost::asio::ip::tcp::v4(), port)),
            started_(false)
        {
		}

        virtual ~TinyCommAsio()
        {
            //TODO: should we close sockets here?
        };

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
                workers_[i] = std::thread([this]()
				{
                    boost::asio::io_service::work work(io_service_); 
                    io_service_.run(); 
                });
            }
        };

        // send/receive
        virtual CommErrors send(const MessagePtr & msg) override
        {
            // pad a uint64_t size at the head of the buffer
            uint64_t size = msg->get_stream_buffer().get_size() + sizeof(uint64_t);
            msg->get_stream_buffer().write_head(size);
            try
            {
                SocketBuffersPtr socket = get_socket(msg->get_remote_addr());
                LockGuard sl(socket->lock);
                boost::asio::write(*(socket->sock),
                    boost::asio::buffer(msg->get_stream_buffer().get_buf(), msg->get_stream_buffer().get_size()));
            }
            catch (std::exception & e)
            {
                WARN("communication error occurred: %s", e.what());
                return CommErrors::SEND_ERROR;
            }            
            return CommErrors::SUCCESS;
        };

        virtual MessagePtr recv() override
        { 
            return receive_queue_.pop();
        };

    private:
        void accepting_thread_func()
        {
            acceptor_.listen();
            while (true)
            {
                asioSocket* sock = new asioSocket(io_service_);
                try
                {
                    acceptor_.accept(*sock);
                    asioEP & remote = sock->remote_endpoint();
                    LockGuard l(sockets_lock_);
                    SocketBuffersPtr & socket = sockets_[remote];
                    if (socket == nullptr)
                    {
                        socket = SocketBuffersPtr(new SocketBuffers());
                    }
                    ASSERT(socket->sock == nullptr, "this socket seems to have connected: %s", EPToString(remote).c_str());
                    socket->sock = sock;
                    post_async_read(socket);
				}
                catch (std::exception & e)
                {
                    ABORT("error occurred: %s", e.what());
                }
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
                socket->sock->async_read_some(boost::asio::buffer(buf, size),
                    [this, socket](const boost::system::error_code& ec, std::size_t bytes_transferred)
                {
                    handle_read(socket, ec, bytes_transferred);
                });
            }
            catch (std::exception & e)
            {
                ABORT("hit an exception: %s", e.what());
            }
        }

        void handle_read(SocketBuffersPtr socket, const boost::system::error_code& ec, std::size_t bytes_transferred)
        {
            if (ec)
            {
                WARN("read error, try to handle failture");
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
                    uint64_t total_size = *(uint64_t*)socket->receive_buffer.get_buf();
                    if (bytes_received_total < total_size)
                    {
                        if (socket->receive_buffer.size() < total_size)
                        {
                            socket->receive_buffer.resize(total_size);
                        }
                    }
                    else
                    {
                        ASSERT(total_size == socket->receive_buffer.get_received_bytes(), "bytes count don't match");
                        LOG("A complete packet is received, size=%lld", total_size);
                        // have received the whole message, pack it into MessagePtr and start receiving next one
                        MessagePtr message(new MessageType);
                        message->set_remote_addr(socket->sock->remote_endpoint());
                        message->get_stream_buffer().set_buf(
                            (char*)socket->receive_buffer.renew_buf(RECEIVE_BUFFER_SIZE), total_size);
                        uint64_t size;
                        // remove the head uint64_t before passing it to RPC
                        message->get_stream_buffer().read(size);
                        receive_queue_.push(message);
                    }
                }
                // no matter what happended, we should post a new read request
                post_async_read(socket);
            }
        }

        void handle_failure(SocketBuffersPtr socket, const boost::system::error_code& ec)
        {
            WARN("a network failure occurred, ec=%d", ec);
            LockGuard l(sockets_lock_);
            LockGuard sl(socket->lock);
            if (socket->sock == nullptr)
            {
                delete socket->sock;
                socket->sock = nullptr;
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
                    WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string(), remote.port(), e.what());
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
		asioAcceptor acceptor_;
        std::mutex sockets_lock_;
        EPSocketMap sockets_;

        std::thread accepting_thread_;
        std::vector<std::thread> workers_;
    };
};