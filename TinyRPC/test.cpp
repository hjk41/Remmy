#include <iostream>
#include <type_traits>
#include <functional>
#include <vector>
#include <map>
#include <chrono>
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
#include <Windows.h>
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
        //cout << "handling a vector of size " << v.size() << endl;
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

    std::vector<char> request;
    int response;
};

const int TEST_PORT = 8082;



int main(int argc, char ** argv)
{
#if 0
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
	asioEP ep(addr.from_string("127.0.0.1"), TEST_PORT);
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
#else
    if (argc != 6)
    {
        cout << "usage: ./testRPC m/s ip port vectorSize nIter" << endl;
        return 1;
    }

	int vectorSize = atoi(argv[4]);
	int nIter = aoti(argv[5]);
	cout << "sending " << nIter <<" requests with vector of size=" << vectorSize << " bytes" << endl;

    if (string(argv[1]) == "m")
    {
        int port = atoi(argv[3]);
        TinyCommBase<asioEP> *test = new TinyCommAsio(port);
        TinyRPCStub<asioEP> *rpc = new TinyRPCStub<asioEP>(test, 2);

        Master master;
        rpc->RegisterProtocol<VectorProtocol>(&master);
        char c;
        cin >> c;
    }
    else
    {
        int port = atoi(argv[3]);
        TinyCommBase<asioEP> *test = new TinyCommAsio(port + 10);
        TinyRPCStub<asioEP> *rpc = new TinyRPCStub<asioEP>(test, 2);

        Master master;
        rpc->RegisterProtocol<VectorProtocol>(&master);

        VectorProtocol vp;
        vp.request.resize(25);
        boost::asio::ip::address addr;
        asioEP ep(addr.from_string(argv[2]), port);
        map<uint64_t, int> hist;
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
		LARGE_INTEGER start, end;
		QueryPerformanceCounter(&start);
        for (int i = 0; i < nIter; i++)
        {          
            rpc->rpc_call(ep, vp);
//            hist[1.8*t]++;
        }
		QueryPerformanceCounter(&end);
		double t = double(end.QuadPart - start.QuadPart) / freq.QuadPart);
		cout << 

		/*
        ofstream out("out.txt");
        int sum = 0;
        for (auto & kv : hist)
        {
            sum += kv.second;
            out << kv.first << "\t" << sum << endl;
        }
		*/
    }

    
#endif


    return 0;
}
#endif