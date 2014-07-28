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
			for (auto & it : recv_data_) {
				free(it.second);
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
		asioSocket *check_socket(const asioEP & remote, asioSocket *sock) {
			//accquire lock
			m_mutex.lock();

			EPSocketMap::iterator it = sockets_.find(remote);
			if (it == sockets_.end())
            {
				if (!sock) {
					sock = new asioSocket(io_service_);
					try {
						sock->connect(remote);
					} catch (std::exception & e) {
						WARN("error connecting to server %s:%d, msg: %s", remote.address().to_string(), remote.port(), e.what());
					}
				}
				it = sockets_.insert(it, std::make_pair(remote, sock));
				LOG("connecting to server: %s:%d", remote.address().to_string(), remote.port());
				StreamBuffer *data = new StreamBuffer(BUFFER_SIZE);
				data->clear(true);
				recv_data_[remote] = data;
				send_buffers_[remote] = NULL;
				sock->async_read_some(boost::asio::buffer(data->get_end(), data->get_remain()), boost::bind(&TinyCommAsio::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
            }
			else {
				sock = it->second;
			}
			
			//release lock
			m_mutex.unlock();
			return sock;
		}

        void sending_thread_func()
        {
            while (true)
            {
                MessagePtr msg = send_queue_.pop();
				asioEP & remote = msg->get_remote_addr();
				asioSocket *sock = check_socket(remote, NULL);


				//if the socket is writing, check the next one
				char *send_buffer = send_buffers_[remote];
				if (send_buffer) {
					send_queue_.push(msg);
					continue;
				}

				// send
                // packet format: size_t fullSize | int64_t seq | uint32_t protocol_id | uint32_t sync | contents
				StreamBuffer & streamBuf = msg->get_stream_buffer();
				size_t size = streamBuf.get_size();
				size_t fullSize = size + sizeof(int64_t) + 2 * sizeof(uint32_t) + sizeof(size_t);
				int64_t seq = msg->get_seq();
				uint32_t protocol_id = msg->get_protocol_id();
				uint32_t sync = msg->get_sync();

				streamBuf.write_head(sync);
				streamBuf.write_head(protocol_id);
				streamBuf.write_head(seq);
				streamBuf.write_head(fullSize);

				send_buffer = streamBuf.get_buf();
				send_buffers_[remote] = send_buffer;

				boost::asio::async_write(*sock, boost::asio::buffer(send_buffer, fullSize), boost::bind(&TinyCommAsio::handle_write, this, boost::asio::placeholders::error, sock));
			}
        }

		void handle_write(const boost::system::error_code& ec, asioSocket *sock)
        {
            if (ec)
            {
                WARN("write error, try to handle failture");
				handle_failture(ec, sock);
            }
            else
            {
				cout << "write finished" << endl;
				send_buffers_[sock->remote_endpoint()] = NULL;
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
                    asioEP & remote = sock->remote_endpoint();
					check_socket(remote, sock);
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
                WARN("read error, try to handle failture");
				handle_failture(ec, sock);
            }
            else
            {
				asioEP & remote = sock->remote_endpoint();
				StreamBuffer *data = recv_data_[remote];
				if (bytes_transferred > 0) {
					cout << "recv " << bytes_transferred << " bytes" << endl;
					data->move_ppos(bytes_transferred);
					if (data->get_size() >= sizeof(size_t)) {
						size_t size = 0;
						data->read(size);
						data->reset_gpos();
						if (data->get_size() == size) {
							int header_size = 0;
							data->read(size);
							header_size += sizeof(size);

							int64_t seq = 0;
							data->read(seq);
							header_size += sizeof(seq);

							uint32_t protocol_id = 0;
							data->read(protocol_id);
							header_size += sizeof(protocol_id);

							uint32_t sync = 0;
							data->read(sync);
							header_size += sizeof(sync);

							MessagePtr message(new MessageType);
							message->set_seq(seq);
							message->set_protocol_id(protocol_id);
							message->set_sync(sync);

							message->set_stream_buffer(*data);
							message->set_remote_addr(sock->remote_endpoint());
							receive_queue_.push(message);

							data = new StreamBuffer(BUFFER_SIZE);
							data->clear(true);
							recv_data_[remote] = data;
						}
						else if (data->get_size() == bytes_transferred) {
							data->reserve(size);
						}
					}
				}
				else {
				}
				sock->async_read_some(boost::asio::buffer(data->get_end(), data->get_remain()), boost::bind(&TinyCommAsio::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock));
            }
        }

		void handle_failture(const boost::system::error_code& ec, asioSocket *sock) {
			m_mutex.lock();

			asioEP & remote = sock->remote_endpoint();
			EPSocketMap::iterator it = sockets_.find(remote);
			if (it != sockets_.end()) {
				sockets_.erase(it);
				WARN("socket %s:%d will be closed with error msg: ", remote.address().to_string(), remote.port());
				cout << ec.message() << endl;
				sock->close();
				send_buffers_[remote] = NULL;
				StreamBuffer *data = recv_data_[remote];
				delete data;
				recv_data_[remote] = NULL;
			}

			m_mutex.unlock();
		}

        bool started_;
        ConcurrentQueue<MessagePtr> send_queue_;
        ConcurrentQueue<MessagePtr> receive_queue_;

		EPSocketMap sockets_;

		EPBufferMap send_buffers_;

		EPDataMap recv_data_;

		asioService io_service_;
		asioAcceptor acceptor_;

        std::thread sending_thread_;
        std::thread accepting_thread_;
        std::vector<std::thread> workers_;

		std::mutex m_mutex;
    };
};