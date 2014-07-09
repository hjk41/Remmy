#pragma once 
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
    public:
        TinyCommAsio(int port)
            : acceptor_(io_service_, asioEP(boost::asio::ip::tcp::v4(), port))
        {
        };

        virtual ~TinyCommAsio()
        {
            for (auto & it : sending_sockets_)
            {
                it.second->close();
            }
            for (auto & it : receiving_sockets_)
            {
                it.second->close();
            }
        };

        // start polling for messages
        virtual void start() override 
        {
            receiving_thread_ = std::thread([this](){receiving_thread_func(); });
            sending_thread_ = std::thread([this](){sending_thread_func(); });
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
                auto it = sending_sockets_.find(msg->get_remote_addr());
                if (it == sending_sockets_.end())
                {
                    asioEP remote = msg->get_remote_addr();
                    asioSocket* sock = new asioSocket(io_service_);
                    try
                    {
                        sock->connect(remote);
                        it = sending_sockets_.insert(it, std::make_pair(remote, sock));
                    }
                    catch (std::exception & e)
                    {
                        WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string(), remote.port(), e.what());
                        continue;
                    }
                }
                // send
                asioSocket* sock = it->second;
                StreamBuffer & streamBuf = msg->get_stream_buffer();
                size_t size = streamBuf.get_size();
                // packet format: 64bit size | contents
                streamBuf.write_head((char*)&size, sizeof(size));
                sock->async_write_some(boost::asio::buffer(streamBuf.get_buf(), streamBuf.get_size()), 
                    [size, this](const boost::system::error_code & error, size_t bytes_write)
                    {ASSERT(!error && size == bytes_write, "We have not implemented error handling yet."); });
            }
        }

        void receiving_thread_func()
        {
            while (true)
            {

            }
        }


        ConcurrentQueue<MessagePtr> send_queue_;
        ConcurrentQueue<MessagePtr> receive_queue_;
        std::unordered_map<asioEP, asioSocket*, EPHasher> sending_sockets_;
        std::unordered_map<asioEP, asioSocket*, EPHasher> receiving_sockets_;

        asioService io_service_;
        asioAcceptor acceptor_;
        std::thread sending_thread_;
        std::thread receiving_thread_;
    };
};