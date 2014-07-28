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

	virtual void handle_request(void *server) {
        response = request;
	}
};

const int TEST_PORT = 8082;

int main()
{
	TinyCommBase<asioEP> *test = new TinyCommAsio(TEST_PORT);
	TinyRPCStub<asioEP> *rpc = new TinyRPCStub<asioEP>(test);

    rpc->RegisterProtocol<EchoProtocol, NULL>();

	rpc->start_serving();

	int ins = 0;
	cin >> ins;
	
	if (ins == 0) {
		cout << "start test" << endl;
		EchoProtocol p;
		p.request = 1000;
		cout << "the request = " << p.request << endl;
		cout << "rpc call" << endl;
		boost::asio::ip::address addr;
		asioEP ep(addr.from_string("10.190.172.62"), TEST_PORT);
		rpc->rpc_call(ep, p);
		cout << "the response = " << p.response << endl;

		p.request = 2000;
		cout << "the request = " << p.request << endl;
		cout << "rpc call" << endl;
		rpc->rpc_call(ep, p);
		cout << "the response = " << p.response << endl;
	}

	Sleep(100000);

    return 0;
}