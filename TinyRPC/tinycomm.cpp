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
	TinyAssert(provided == MPI_THREAD_SERIALIZED || provided == MPI_THREAD_MULTIPLE, "MPI does not support multiple threads");
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

static void PollingFunction(void * comm_ptr)
{
	TinyCommMPI * comm = static_cast<TinyCommMPI *>(comm_ptr);
	comm->PollingThreadFunc();
}

void TinyCommMPI::PollingThreadFunc()
{
	while(1)
	{
		MPI_Status status;
		int success = 0;
		while(!success)
		{
			if (_kill_threads)
				return;
			{
			TinyAutoLock l(_mpi_lock);
			MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &success, &status);
			}
			if(!success)
				yield_cpu();
		}
		int n_bytes;
		MPI_Get_count(&status, MPI_CHAR, &n_bytes);
		char * buf = new char[n_bytes];
		{
		TinyAutoLock l(_mpi_lock);
		MPI_Recv(buf, n_bytes, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
		}
		TinyMessageBuffer * msg = new TinyMessageBuffer();
		TinyLog(LOG_NORMAL, "received message from %d with size=%d\n", status.MPI_SOURCE, n_bytes);
		msg->set_remote_rank(status.MPI_SOURCE);
		msg->set_buf(buf, n_bytes);
		_received_messages.push(msg);
	}
}

void TinyCommMPI::start_polling()
{
	// start polling thread
	_polling_thread = new TinyThread(PollingFunction, this);
}

void TinyCommMPI::send(int who, TinyMessageBuffer & buf)
{
	TinyAutoLock l(_mpi_lock);
	MPI_Send(buf.get_buf(), static_cast<int>(buf.gsize()), MPI_CHAR, who, 0, MPI_COMM_WORLD);
}

TinyMessageBuffer * TinyCommMPI::recv()
{
	return _received_messages.pop();
}

void TinyCommMPI::barrier()
{
	//XXX Caution !!! This may cause deadlock if the thread calling barrier needs 
	//	to wait for some result from other process, and that process has called 
	//	barrier(), thus holding all the transmissions...
	TinyAutoLock l(_mpi_lock);
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
