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
#include "comm_asio.h"
#include "tinyrpc.h"
using namespace tinyrpc;

static inline double GetTime() {
    using namespace std::chrono;
    high_resolution_clock::duration tp =
        high_resolution_clock::now().time_since_epoch();
    return (double)tp.count() * high_resolution_clock::period::num
        / high_resolution_clock::period::den;
}

class EchoProtocol : public ProtocolTemplate<int, int> {
public:
	virtual uint32_t UniqueId() {
		return 0;
	}

	virtual void HandleRequest(void *server) {
        response = request;
	}
};

class Master {
public:
    size_t handle(const std::vector<char> & v) {
        cout << "handling a vector of size " << v.size() << endl;
        return v.size();
    }
};

class VectorProtocol : public ProtocolBase {
public:
    virtual uint32_t UniqueId() override {
        return 1;
    }

    virtual void MarshallRequest(StreamBuffer & buf) {
        Serialize(buf, request);
    }

    virtual void MarshallResponse(StreamBuffer & buf) {
        Serialize(buf, response);
    }

    virtual void UnmarshallRequest(StreamBuffer & buf) {
        Deserialize(buf, request);
    }

    virtual void UnmarshallResponse(StreamBuffer & buf) {
        Deserialize(buf, response);
    }

    virtual void HandleRequest(void *server) {
        Master * master = (Master*)server;
        response = master->handle(request);
    }

    std::vector<char> request;
    size_t response;
};

const int TEST_PORT = 8082;

int testAsio(int argc, char** argv) {    
    return 0;
}

int main(int argc, char ** argv) {
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
	asio::ip::address addr;
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
    if (argc < 2) {
        cout << "usage: ./testRPC m/s ip port vectorSize nIter" << endl;
        return 1;
    }

    if (string(argv[1]) == "m") {
        if (argc < 4) {
            cout << "usage: ./testRPC m port nPorts" << endl;
            return 1;
        }
        int port = atoi(argv[2]);
        int n_ports = atoi(argv[3]);
        std::vector<std::thread> threads;
        for (int i = 0; i < n_ports; i++) {
            threads.emplace_back([=]() {
                TinyCommAsio test(port + i);
                TinyRPCStub<AsioEP> rpc(&test, 2);

                Master master;
                rpc.RegisterProtocol<VectorProtocol>(&master);
                cout << "listening on port " << port << endl;
                char c;
                cin >> c;
            });
        }
        for (auto& t : threads) t.join();
    }
    else {
        if (argc < 7) {
            cout << "usage: ./testRPC s ip port vectorSize nIter nClients" << endl;
            return 1;
        }
        asio::ip::address addr;
        auto host = addr.from_string(argv[2]);
        int port = atoi(argv[3]);
        int vectorSize = atoi(argv[4]);
        int n_iter = atoi(argv[5]);
        int n_clients = atoi(argv[6]);
        cout << "sending " << n_iter <<" requests with vector of size=" 
            << vectorSize << " bytes with " << n_clients << " clients" << endl;

        std::vector<std::thread> threads;
        double t1 = GetTime();
        for (int i = 0; i < n_clients; i++) {
            threads.emplace_back([=]() {
                AsioEP ep(host, port + i);
                TinyCommAsio test(0);
                TinyRPCStub<AsioEP> rpc(&test, 2);
                Master master;
                rpc.RegisterProtocol<VectorProtocol>(&master);
                VectorProtocol vp;
                vp.request.resize(vectorSize);
                for (int i = 0; i < n_iter; i++) {
                    rpc.RpcCall(ep, vp);
                    cout << vp.response << endl;
                }
            });
        }        
        for (auto& t : threads) t.join();
        double t2 = GetTime();
        cout << double(vectorSize) * n_iter * n_clients / 1024 / 1024 / (t2 - t1) << " MB/s" << endl;
    }
    
#endif

    return 0;
}
#endif
