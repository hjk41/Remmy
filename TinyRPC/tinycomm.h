#pragma once

#include <stdint.h>
#include "tinylock.h"
#include "tinythread.h"
#include "tinymessagequeue.h"
#include "singleton.h"

namespace TinyRPC
{
	
const static int TINY_ANY_SOURCE = -1;

class TinyCommBase
{
public:
	TinyCommBase(){};
	virtual ~TinyCommBase(){};

	// start polling for messages
	virtual void start_polling()=0;
	// send/receive
	virtual void send(int who, TinyMessageBuffer &)=0;
	virtual TinyMessageBuffer * recv()=0;
	// wake up nodes waiting to receive
	virtual void wake_all_recv_threads()=0;

	// other
	virtual void barrier() = 0;
	virtual int get_num_nodes() = 0;
	virtual int get_node_id() = 0;
};

class TinyCommMPI : public TinyCommBase, public Singleton<TinyCommMPI>
{
public:
	TinyCommMPI(){};
	TinyCommMPI(const TinyCommMPI &){};
	TinyCommMPI & operator = (const TinyCommMPI &);
	~TinyCommMPI(){};
public:
	// initialize communication, must be called before get_instance
	void init(int * argc, char *** argv);
	// call MPI_Finalize before exit
	void finalize();
	// start processing messages
	virtual void start_polling();
	// send/receive
	virtual void send(int who, TinyMessageBuffer &);
	virtual TinyMessageBuffer * recv();
	// wake up nodes waiting to receive
	virtual void wake_all_recv_threads();
	// other
	virtual void barrier();
	virtual int get_num_nodes();
	virtual int get_node_id();
public:
	void PollingThreadFunc();
private:
	int _rank;
	int _size;
	// threads
	TinyThread * _polling_thread;
	TinyMessageQueue _received_messages;
	int _kill_threads;
	TinyLock _mpi_lock;
};

};
