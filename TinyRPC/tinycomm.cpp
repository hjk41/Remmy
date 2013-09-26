#include "tinycomm.h"
#include "logging.h"
#include <mpi.h>
#include <iostream>

#ifdef WIN32
#include <windows.h>
static void yield_cpu()
{
	Sleep(0);
}
#else
#include <unistd.h>
static void yield_cpu()
{
	usleep(0);
}
#endif

namespace TinyRPC
{
TinyCommMPI * TinyCommMPI::_instance = NULL;

void TinyCommMPI::init(int * argc, char *** argv)
{
	// init MPI
	int provided;
	MPI_Init_thread(argc, argv, MPI_THREAD_SERIALIZED, &provided);
	TinyAssert(provided == MPI_THREAD_SERIALIZED, "MPI does not support multiple threads");
	MPI_Comm_rank(MPI_COMM_WORLD, &_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &_size);
	_kill_threads = false;
}

void TinyCommMPI::finalize()
{
	_kill_threads = true;
	// wait for polling thread to complete
	_polling_thread->wait();
	delete _polling_thread;
	MPI_Finalize();
}

void TinyCommMPI::wake_all_recv_threads()
{
	_received_messages.wake_all_for_kill();
}

static void PollingFunction(void * queue_ptr, void * terminate_ptr)
{
	TinyMessageQueue * msg_queue = static_cast<TinyMessageQueue*>(queue_ptr);
	int * terminate = static_cast<int*>(terminate_ptr);
	while(1)
	{
		MPI_Status status;
		int success = 0;
		while(!success)
		{
			if (*terminate)
				return;
			MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &success, &status);
			if(!success)
				yield_cpu();
		}
		int n_bytes;
		MPI_Get_count(&status, MPI_CHAR, &n_bytes);
		char * buf = new char[n_bytes];
		MPI_Recv(buf, n_bytes, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
		TinyMessageBuffer * msg = new TinyMessageBuffer();
		TinyLog(LOG_NORMAL, "received message from %d with size=%d\n", status.MPI_SOURCE, n_bytes);
		msg->set_remote_rank(status.MPI_SOURCE);
		msg->set_buf(buf, n_bytes);
		msg_queue->push(msg);
	}
}

void TinyCommMPI::start_polling()
{
	// start polling thread
	_polling_thread = new TinyThread(PollingFunction, &_received_messages, &_kill_threads);
}

void TinyCommMPI::send(int who, TinyMessageBuffer & buf)
{
	MPI_Send(buf.get_buf(), static_cast<int>(buf.gsize()), MPI_CHAR, who, 0, MPI_COMM_WORLD);
}

TinyMessageBuffer * TinyCommMPI::recv()
{
	return _received_messages.pop();
}

void TinyCommMPI::barrier()
{
	MPI_Barrier(MPI_COMM_WORLD);
}

int TinyCommMPI::get_num_nodes()
{
	return _size;
}

int TinyCommMPI::get_node_id()
{
	return _rank;
}

};