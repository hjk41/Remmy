#include "tinyrpc.h"
#include "logging.h"
#include <mpi.h>

namespace TinyRPC
{

TinyRPCStub * TinyRPCStub::_instance = NULL;
static const uint32_t ASYNC_RPC_CALL = 1;
static const uint32_t SYNC_RPC_CALL = 0;

void TinyRPCStub::init(int * argc, char *** argv)
{
	_seq_num = 1;
	// initalize communication
	_comm = TinyCommMPI::get_instance();
	_comm->init(argc, argv);
	_kill_threads = false;
};

TinyRPCStub::~TinyRPCStub()
{
	_kill_threads = true;

	_comm->wake_all_recv_threads();
	_sleeping_list.wake_all_for_killing();
	for (int i=0; i<NUM_WORKER_THREADS; i++)
	{
		_worker_threads[i]->wait();
		delete _worker_threads[i];
	}

	_comm->finalize();
	_comm->delete_instance();

}

int64_t TinyRPCStub::get_new_seq_num()
{
	TinyAutoLock l(_seq_lock);
	if (_seq_num >= INT64_MAX-1)
		_seq_num = 1;
	return _seq_num++;
}

//---------------------------------
// message format:
//      int64_t seq_num:		positive means request, negtive means response
//		uint32_t protocol_id:	protocol id, must be registered in both client and server
//	for request
//		uint32_t async:			0 means sync call, 1 means async call
//		char * buf:				request of the protocol
//	for response
//		char * buf:				response of the protocol

void TinyRPCStub::handle_message()
{
	TinyMessageBuffer * buf = _comm->recv();
	if (_kill_threads)
		return;
	// get seq number, test if request or response
	int64_t seq;
	UnMarshall(*buf, seq);
	// get the protocol handle
	uint32_t protocol_id;
	UnMarshall(*buf, protocol_id);

	if (seq < 0)
	{
		// a response
		seq = -seq;
		// unmarshal response
		ProtocolBase * protocol = _sleeping_list.get_response_ptr(seq);
		if (_kill_threads)
			return;
		protocol->unmarshall_response(*buf);
		delete buf;
		// wake up waiting thread
		_sleeping_list.signal_response(seq);
	}
	else
	{
		if (_protocol_factory.find(protocol_id) == _protocol_factory.end() )
		{
			// unsupported call, register the func with the server please!
			int remote = buf->get_remote_rank();
			TinyLog(LOG_ERROR, "Unsupported call from %d, request ID=%d", remote, protocol_id);
			delete buf;
			return;
		}
		ProtocolBase * protocol = _protocol_factory[protocol_id].first->create_protocol();
		// a request
		uint32_t is_async;
		UnMarshall(*buf, is_async);
		// handle request
		protocol->unmarshall_request(*buf);
		protocol->handle_request(_protocol_factory[protocol_id].second);
		// send response if sync call
		if (!is_async)
		{
			TinyMessageBuffer out_buffer;
			Marshall(out_buffer, -seq);
			Marshall(out_buffer, protocol_id);
			protocol->marshall_response(out_buffer);	
			TinyLog(LOG_NORMAL, "responding to %d with seq=%d, protocol_id=%d\n", buf->get_remote_rank(), -seq, protocol_id);
			_comm->send(buf->get_remote_rank(), out_buffer);
		}
		delete buf;
		delete protocol;
	}
}

void WorkerFunction(void * rpc_ptr, void * kill_ptr)
{
	TinyRPCStub * rpc = static_cast<TinyRPCStub *>(rpc_ptr);
	bool * kill_threads = static_cast<bool *>(kill_ptr);
	while(*kill_threads == false)
	{
		rpc->handle_message();
	}
}

void TinyRPCStub::start_serving()
{
	_comm->start_polling();
	// start threads
	for (int i=0; i<NUM_WORKER_THREADS; i++)
	{
		TinyThread * worker = new TinyThread(WorkerFunction, this, &_kill_threads);
		_worker_threads.push_back(worker);
	}
}

uint32_t TinyRPCStub::rpc_call(int who, ProtocolBase & protocol)
{
	TinyMessageBuffer message;
	uint64_t seq = get_new_seq_num();
	Marshall(message, seq);
	Marshall(message, protocol.ID());
	Marshall(message, SYNC_RPC_CALL);
	protocol.marshall_request(message);
	// send message
	_sleeping_list.set_response_ptr(seq, &protocol);
	_comm->send(who, message);
	// wait for signal
	_sleeping_list.wait_for_response(seq);
	if (_kill_threads)
			return KILLING_THREADS;
	return SUCCESS;
}

uint32_t TinyRPCStub::rpc_call_async(int who, ProtocolBase & protocol)
{
	TinyMessageBuffer message;
	uint64_t seq = get_new_seq_num();
	Marshall(message, seq);
	Marshall(message, protocol.ID());
	Marshall(message, ASYNC_RPC_CALL);
	protocol.marshall_request(message);
	// send message
	_comm->send(who, message);
	return SUCCESS;
}

};