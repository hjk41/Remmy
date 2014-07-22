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
	typedef boost::asio::ip::tcp::endpoint asioEP;
	typedef boost::asio::ip::tcp::socket asioSocket;
	typedef boost::asio::ip::tcp::acceptor asioAcceptor;
	typedef boost::asio::io_service asioService;
	typedef boost::asio::strand asioStrand;
	typedef boost::asio::ip::address asioAddr;

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
        const static int BUFFER_SIZE = 4096;

        typedef std::unordered_map<asioEP, asioSocket *, EPHasher> EPSocketMap;
		typedef std::unordered_map<asioEP, char *, EPHasher> EPBufferMap;
		typedef std::unordered_map<asioEP, StreamBuffer *, EPHasher> EPDataMap;

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
            for (auto & it : sockets_)
            {
                it.second->close();
                delete it.second;
            }
			for (auto & it : data_) {
				delete it.second;
			}
			for (auto & it : buffers_) {
				delete it.second;
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
                workers_[i] = std::thread([this]()
                    {
                    boost::asio::io_service::work work(io_service_); 
                    io_service_.run(); 
                });
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
                EPSocketMap::iterator it = sockets_.find(msg->get_remote_addr());
				asioEP remote = msg->get_remote_addr();
                if (it == sockets_.end())
                {
                    asioSocket* sock = new asioSocket(io_service_);
                    try
                    {
                        sock->connect(remote);
                        it = sockets_.insert(it, std::make_pair(remote, sock));
						LOG("connecting to server: %s:%d", remote.address().to_string(), remote.port());
                    }
                    catch (std::exception & e)
                    {
                        WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string(), remote.port(), e.what());
                        continue;
                    }
					data_[remote] = new StreamBuffer();
					buffers_[remote] = (char *)malloc(BUFFER_SIZE);
                }

				asioSocket* sock = it->second;
				StreamBuffer & data = *data_[remote];
				data.clear();
				char *receive_buffer = buffers_[remote];

				// send
                // packet format: size_t fullSize | int64_t seq | uint32_t protocol_id | uint32_t sync | contents
				StreamBuffer & streamBuf = msg->get_stream_buffer();
				size_t size = streamBuf.get_size();
				size_t fullSize = size + sizeof(int64_t) + 2 * sizeof(uint32_t) + sizeof(size_t);
				send_buffer = (char *)malloc(fullSize);
				int64_t seq = msg->get_seq();
				uint32_t protocol_id = msg->get_protocol_id();
				uint32_t sync = msg->get_sync();
				memcpy(send_buffer, (void*)&fullSize, sizeof(fullSize));
				memcpy(send_buffer + sizeof(fullSize), (void*)&seq, sizeof(seq));
                memcpy(send_buffer + sizeof(fullSize) + sizeof(seq), (void*)&protocol_id, sizeof(protocol_id));
				memcpy(send_buffer + sizeof(fullSize) + sizeof(seq) + sizeof(protocol_id), (void*)&sync, sizeof(sync));
				memcpy(send_buffer + sizeof(fullSize) + sizeof(seq) + sizeof(protocol_id) + sizeof(sync), (void*)streamBuf.get_buf(), size);

				boost::asio::async_write(*sock, boost::asio::buffer(send_buffer, fullSize), boost::bind(&TinyCommAsio::handle_write, this, boost::asio::placeholders::error, sock));

				sock->async_read_some(boost::asio::buffer(receive_buffer, BUFFER_SIZE), boost::bind(&TinyCommAsio::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
			}
        }

		void handle_write(const boost::system::error_code& ec, asioSocket *sock)
        {
            if (ec)
            {
                WARN("write error");
            }
            else
            {
				cout << "write finished" << endl;
				free(send_buffer);
            }
        }

        void accepting_thread_func()
        {
            acceptor_.listen();
            while (true)
            {
                asioSocket* sock = new asioSocket(io_service_);
                try
                {
                    acceptor_.accept(*sock);
					// insert into the receiving sockets
                    asioEP & remote = sock->remote_endpoint();
					EPSocketMap::iterator it = sockets_.find(remote);
                    if (it == sockets_.end())
                    {
                        LOG("adding new receiving socket: %s:%d", remote.address().to_string(), remote.port());
                        it = sockets_.insert(it, std::make_pair(remote, sock));
						data_[remote] = new StreamBuffer();
						StreamBuffer & data = *data_[remote];
						data.clear();
						buffers_[remote] = (char *)malloc(BUFFER_SIZE);
						char *receive_buffer = buffers_[remote];
						sock->async_read_some(boost::asio::buffer(receive_buffer, BUFFER_SIZE), boost::bind(&TinyCommAsio::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
                    }
                    else
                    {
                        WARN("socket reconnected? %s:%d", remote.address().to_string(), remote.port());
                        it->second = sock;
                    }
				}
                catch (exception & e)
                {
					cout << e.what() << endl;
                }
            }
        }

		void handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred, asioSocket *sock)
        {
            if (ec)
            {
                WARN("read error");
				cout << ec.message() << endl;
            }
            else
            {
				asioEP & remote = sock->remote_endpoint();
				StreamBuffer & data = *data_[remote];
				data.clear();
				char *receive_buffer = buffers_[remote];
				if (bytes_transferred > 0) {
					cout << "recv " << bytes_transferred << " bytes" << endl;
					data.write(receive_buffer, bytes_transferred);
					if (data.get_size() >= sizeof(size_t)) {
						size_t size = 0;
						data.read(size);
						data.reset_gpos();
						// read has finished
						if (data.get_size() == size) {
							// packet format: size_t fullSize | int64_t seq | uint32_t protocol_id | uint32_t sync | contents
							int header_size = 0;
							data.read(size);
							header_size += sizeof(size);
							int64_t seq = 0;
							data.read(seq);
							header_size += sizeof(seq);
							uint32_t protocol_id = 0;
							data.read(protocol_id);
							header_size += sizeof(protocol_id);
							uint32_t sync = 0;
							data.read(sync);
							header_size += sizeof(sync);
							StreamBuffer buf;
							buf.write(data.get_buf() + header_size, size - header_size);
							MessagePtr message(new MessageType);
							message->set_seq(seq);
							message->set_protocol_id(protocol_id);
							message->set_sync(sync);
							message->get_stream_buffer().clear();
							message->get_stream_buffer().write(data.get_buf() + header_size, size - header_size);
							message->set_remote_addr(sock->remote_endpoint());
							receive_queue_.push(message);
							data.clear();
						}
					}
				}
				else {
					data.clear();
				}
				sock->async_read_some(boost::asio::buffer(receive_buffer, BUFFER_SIZE), boost::bind(&TinyCommAsio::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
            }
        }


        bool started_;
        ConcurrentQueue<MessagePtr> send_queue_;
        ConcurrentQueue<MessagePtr> receive_queue_;

		EPSocketMap sockets_;

		EPBufferMap buffers_;

		EPDataMap data_;

		asioService io_service_;
		asioAcceptor acceptor_;

        std::thread sending_thread_;
        std::thread accepting_thread_;
        std::vector<std::thread> workers_;

		char *send_buffer;
    };
};