//#include <iostream>
//using namespace std;
//
//#include "tinyrpc.h"
//using namespace TinyRPC;
//
//class EchoProtocol : public ProtocolTemplate<int, int>
//{
//public:
//	virtual void handle_request(void * server)
//	{
//		response = request;
//	}
//
//	virtual uint32_t ID()
//	{
//		return 0;
//	}
//};
//
//
//
//int main(int argc, char ** argv)
//{
//	TinyRPCStub * rpc = TinyRPCStub::get_instance();
//	rpc->init(&argc, &argv);
//
//	int rank = rpc->get_node_id();
//	
//	if(rank == 0)
//	{
//		char c;
//		cin>>c;
//	}
//	rpc->barrier();
//
//
//	rpc->RegisterProtocol<EchoProtocol, NULL>();
//	rpc->start_serving();
//
//	if (rank != 0)
//	{
////		MPI_Send(&rank, sizeof(rank), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
//		EchoProtocol p;
//		p.request = 100 + rank;
//		rpc->rpc_call(0, p);
//		cout<<p.response<<endl;
//	}
//
//	rpc->barrier();
//
//	rpc->delete_instance();
//	return 0;
//}

#include <iostream>
#include <type_traits>
#include <functional>
using namespace std;

#include "streambuffer.h"
#include "message.h"
#include "commAsio.h"
#include "tinyrpc.h"
using namespace TinyRPC;

class EchoProtocol : public ProtocolTemplate<int, int>
{
public:
	virtual uint32_t get_id() {
		return 0;
	}

	virtual void handle_request(StreamBuffer & buf) {
		buf_.clear();
		buf_.write(buf.get_buf(), buf.get_size());
	}
};

int main()
{
	TinyCommBase<asioEP> *test = new TinyCommAsio(TEST_PORT);
	TinyRPCStub<asioEP> *rpc = new TinyRPCStub<asioEP>(test);

    rpc->RegisterProtocol<EchoProtocol, NULL>();

	rpc->start_serving();

	cout << "start test" << endl;
	EchoProtocol p;
	p.request = 1000;
	cout << "the request = " << p.request << endl;
	p.set_request(p.request);
	cout << "rpc call" << endl;
	rpc->rpc_call(0, p);
	cout << "the response = " << p.response << endl;

    return 0;
}