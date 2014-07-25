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
		buf_.clear(true);
		buf_.write(buf.get_buf(), buf.get_size());
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
		p.set_request(p.request);
		cout << "rpc call" << endl;
		rpc->rpc_call("", 0, p);
		cout << "the response = " << p.response << endl;

		p.request = 2000;
		cout << "the request = " << p.request << endl;
		p.set_request(p.request);
		cout << "rpc call" << endl;
		rpc->rpc_call("", 0, p);
		cout << "the response = " << p.response << endl;
	}

	Sleep(10000);

    return 0;
}