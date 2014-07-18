#pragma once 
#include <array>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
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
    /*using asioEP = boost::asio::ip::tcp::endpoint;
    using asioSocket = boost::asio::ip::tcp::socket;
    using asioAcceptor = boost::asio::ip::tcp::acceptor;
    using asioService = boost::asio::io_service;
    using asioStrand = boost::asio::strand;*/

	typedef boost::asio::ip::tcp::endpoint asioEP;
	typedef boost::asio::ip::tcp::socket asioSocket;
	typedef boost::asio::ip::tcp::acceptor asioAcceptor;
	typedef boost::asio::io_service asioService;
	typedef boost::asio::strand asioStrand;
	typedef boost::asio::ip::address asioAddr;

	static const int TEST_PORT = 8081;
	static const asioAddr ADDR;
	static const asioEP LOCAL_EP(ADDR.from_string("127.0.0.1"), TEST_PORT);


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
            : acceptor_(io_service_, LOCAL_EP),
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

                // packet format: int64_t seq | uint32_t protocol_id | uint32_t sync | size_t size | contents
				StreamBuffer & streamBuf = msg->get_stream_buffer();
				size_t size = streamBuf.get_size();
				size_t fullSize = size + sizeof(int64_t) + 2 * sizeof(uint32_t) + sizeof(size_t);
				void *fullData = malloc(fullSize);
				int64_t seq = msg->get_seq();
				uint32_t protocol_id = msg->get_protocol_id();
				uint32_t sync = msg->get_sync();
				memcpy(fullData, (void*)&seq, sizeof(seq));
				memcpy(&fullData + sizeof(seq), (void*)&protocol_id, sizeof(protocol_id));
				memcpy(&fullData + sizeof(seq) + sizeof(protocol_id), (void*)&sync, sizeof(sync));
                memcpy(&fullData + sizeof(seq) + sizeof(protocol_id) + sizeof(sync), (void*)&size, sizeof(size));
				memcpy(&fullData + sizeof(seq) + sizeof(protocol_id) + sizeof(sync) + sizeof(size), (void*)streamBuf.get_buf(), size);
				boost::asio::async_write(*sock, boost::asio::buffer(fullData, fullSize), 
                    [size, this](const boost::system::error_code & error, size_t bytes_write)
                    {ASSERT(!error && size == bytes_write, "We have not implemented error handling yet."); });
				//free(fullData);
            }
        }

        void accepting_thread_func()
        {
            acceptor_.listen();
            while (true)
            {
                asioSocket* sock = new asioSocket(io_service_);
				asioStrand* strand = new asioStrand(io_service_);
                try
                {
                    acceptor_.accept(*sock);
                    // insert into the receiving sockets
                    asioEP & remoteEP = sock->remote_endpoint();
                    EPMap::iterator it = receiving_sockets_.find(remoteEP);
                    if (it == receiving_sockets_.end())
                    {
                        LOG("adding new receiving socket: %s:%d", remoteEP.address().to_string(), remoteEP.port());
                        it = receiving_sockets_.insert(it, std::make_pair(remoteEP, SocketStrand(sock, strand)));
                    }
                    else
                    {
                        WARN("socket reconnected? %s:%d", remoteEP.address().to_string(), remoteEP.port());
                        it->second = SocketStrand(sock, strand);
                    }
                    /*char * receive_buffer = (char*)malloc(BUFFER_SIZE);
					memset(receive_buffer, '0', BUFFER_SIZE);
					cout << receive_buffer[0] << endl;*/
					boost::array<char, 8192> receive_buffer;
					receive_buffer.data()[0]='\0';
					//sock->async_read_some(buf, [sock](const boost::system::error_code & ec, std::size_t bytes_write){receive_handler(ec, bytes_write);});
                    boost::asio::async_read(*sock, boost::asio::buffer(receive_buffer, 1), boost::bind(&TinyCommAsio::receive_handler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
					//sock->async_read_some(boost::asio::buffer(receive_buffer), boost::bind(&TinyCommAsio::receive_handler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
					/*for (int i = 0; i < 25; ++i)
						cout << receive_buffer[i] << endl;*/
				}
                catch (exception & e)
                {
					cout << e.what() << endl;
                }
            }
        }

		void receive_handler(const boost::system::error_code& ec, std::size_t bytes_transferred, asioSocket *sock)
        {
            if (ec)
            {
                WARN("receive error");
            }
            else
            {
				if (bytes_transferred > 0) {
					//sock->async_read_some
				}
				else {
				
				}
				cout << bytes_transferred << endl;
				cout << sock->remote_endpoint().address().to_string() << endl;
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