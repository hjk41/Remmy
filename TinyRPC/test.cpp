#include <iostream>
#include <type_traits>
#include <functional>
#include <vector>
using namespace std;


#ifdef COMPILE_ONLY

#include "streambuffer.h"
#include "protocol.h"
#include "tinyrpc.h"
#include "commAsio.h"

int main()
{
    return 0;
}

#else

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

class Master
{
public:
    int handle(const std::vector<int> & v)
    {
        cout << "handling a vector of size " << v.size() << endl;
        return (int)v.size();
    }
};

class VectorProtocol : public ProtocolBase
{
public:
    virtual uint32_t get_id()
    {
        return 1;
    }

    virtual void marshall_request(StreamBuffer & buf)
    {
        Serialize(buf, request);
    }

    virtual void marshall_response(StreamBuffer & buf)
    {
        Serialize(buf, response);
    }

    virtual void unmarshall_request(StreamBuffer & buf)
    {
        Deserialize(buf, request);
    }

    virtual void unmarshall_response(StreamBuffer & buf)
    {
        Deserialize(buf, response);
    }

    virtual void handle_request(void *server)
    {
        Master * master = (Master*)server;
        response = master->handle(request);
    }

    std::vector<int> request;
    int response;
};

const int TEST_PORT = 8082;



int main()
{
	TinyCommBase<asioEP> *test = new TinyCommAsio(TEST_PORT);
	TinyRPCStub<asioEP> *rpc = new TinyRPCStub<asioEP>(test, 2);

    rpc->RegisterProtocol<EchoProtocol>(NULL);
    Master master;
    rpc->RegisterProtocol<VectorProtocol>(&master);

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

    VectorProtocol vp;
    vp.request.resize(12);
    cout << "vector.size() = " << vp.request.size() << endl;
    rpc->rpc_call(ep, vp);
    cout << "response = " << vp.response << endl;

    char c;
    cin >> c;

    return 0;
}
#endif