#pragma once 
#include <array>
#include <boost/asio.hpp>
#include <exception>
#include <string>
#include <thread>
#include <unordered_map>
#include "concurrentqueue.h"
#include "logging.h"
#include "streambuffer.h"
#include "tinycomm.h"
#include "message.h"

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "CommAsio"

namespace TinyRPC
{
    using asioEP = boost::asio::ip::tcp::endpoint;
    using asioSocket = boost::asio::ip::tcp::socket;
    using asioAcceptor = boost::asio::ip::tcp::acceptor;
    using asioService = boost::asio::io_service;
    using asioStrand = boost::asio::strand;

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
        struct SocketStrand
        {
            SocketStrand() : socket(nullptr), strand(nullptr){};
            SocketStrand(asioSocket * sock, asioStrand * stran)
                : socket(sock),
                strand(stran)
            {}
            asioSocket * socket;
            asioStrand * strand;
        };

        const static int NUM_WORKERS = 8;
        const static int BUFFER_SIZE = 4096;
        typedef std::unordered_map<asioEP, SocketStrand, EPHasher> EPMap;
    public:
        TinyCommAsio(int port)
            : acceptor_(io_service_, asioEP(boost::asio::ip::tcp::v4(), port)),
            started_(false)
        {
        };

        virtual ~TinyCommAsio()
        {
            // TODO: we might want to use the sending thread and receiving thread to 
            // close the sockets: there is still very low probability that we might have
            // a data race here.
            for (auto & it : sending_sockets_)
            {
                it.second.socket->close();
                delete it.second.socket;
                delete it.second.strand;
            }
            for (auto & it : receiving_sockets_)
            {
                it.second.socket->close();
                delete it.second.socket;
                delete it.second.strand;
            }
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
            sending_thread_ = std::thread([this](){sending_thread_func(); });
            workers_.resize(NUM_WORKERS);
            for (int i = 0; i < NUM_WORKERS; i++)
            {
                workers_[i] = std::thread([this](){io_service_.run(); });
            }
        };

        // send/receive
        virtual void send(const MessagePtr & msg) override
        {
            send_queue_.push(msg);
        };

        virtual MessagePtr recv() override
        { 
            return receive_queue_.pop();
        };
    private:
        void sending_thread_func()
        {
            while (true)
            {
                MessagePtr msg = send_queue_.pop();
                // get socket
                EPMap::iterator it = sending_sockets_.find(msg->get_remote_addr());
                if (it == sending_sockets_.end())
                {
                    asioEP remote = msg->get_remote_addr();
                    asioSocket* sock = new asioSocket(io_service_);
                    asioStrand* strand = new asioStrand(io_service_);
                    try
                    {
                        sock->connect(remote);
                        it = sending_sockets_.insert(it, std::make_pair(remote, SocketStrand(sock, strand)));
                    }
                    catch (std::exception & e)
                    {
                        WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string(), remote.port(), e.what());
                        continue;
                    }
                }
                // send
                asioSocket* sock = it->second.socket;
                asioStrand* strand = it->second.strand;
                StreamBuffer & streamBuf = msg->get_stream_buffer();
                // packet format: 64bit size | contents
                size_t size = streamBuf.get_size();
                streamBuf.write_head(size);

                boost::asio::async_write(*sock, boost::asio::const_buffer(streamBuf.get_buf(), streamBuf.get_size()), 
                    [size, this](const boost::system::error_code & error, size_t bytes_write)
                    {ASSERT(!error && size == bytes_write, "We have not implemented error handling yet."); });
            }
        }

        void accepting_thread_func()
        {
            acceptor_.listen();
            while (true)
            {
                asioSocket * sock = new asioSocket(io_service_);
                try
                {
                    acceptor_.accept(*sock);
                    // insert into the receiving sockets
                    asioEP & remoteEP = sock->remote_endpoint();
                    EPMap::iterator it = receiving_sockets_.find(remoteEP);
                    if (it == receiving_sockets_.end())
                    {
                        LOG("adding new receiving socket: %s:%d", remoteEP.address().to_string(), remoteEP.port());
                        it = receiving_sockets_.insert(it, std::make_pair(remoteEP, sock));
                    }
                    else
                    {
                        WARN("socket reconnected? %s:%d", remoteEP.address().to_string(), remoteEP.port());
                        it->second = sock;
                    }
                    char * receive_buffer = (char*)malloc(BUFFER_SIZE);
                    auto buf = boost::asio::buffer(receive_buffer, BUFFER_SIZE);
                    sock->async_read_some(buf, [receive_buffer]()
                        {
                            
                        });
                }
                catch (exception & e)
                {
                }
            }
        }

        void receive_handler(char * receive_buffer, asioSocket * sock, size_t already_received,
            const boost::system::error_code &ec, std::size_t bytes_transferred)
        {
            if (ec)
            {
                WARN("receive error");
            }
            else
            {
                if (already_received + bytes_transferred < sizeof(size_t))
                {

                }
                // packet format: 64bit size | contents
                MessagePtr msg = make_shared<MessageType>();
                StreamBuffer & streamBuf = msg->get_stream_buffer();
                streamBuf.set_buf(receive_buffer, bytes_transferred);
                size_t size;
                streamBuf.read(size);
                bytes_transferred -= sizeof(size);
                if (bytes_transferred < size)
                {
                    // we haven't received the whole package, wait for the rest
                    char * rest = new char[size - bytes_transferred];
                    
                }
            }
        }


        bool started_;
        ConcurrentQueue<MessagePtr> send_queue_;
        ConcurrentQueue<MessagePtr> receive_queue_;
        EPMap sending_sockets_;
        EPMap receiving_sockets_;

        asioService io_service_;
        asioAcceptor acceptor_;

        std::thread sending_thread_;
        std::thread accepting_thread_;
        std::vector<std::thread> workers_;
    };
};