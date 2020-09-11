#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
using namespace std;

#include "remmy/remmy.h"
using namespace remmy;

struct ComplexType {
    int x;
    double y;
    std::string z;

    void Serialize(StreamBuffer& buf) const {
        remmy::Serialize(buf, x);
        remmy::Serialize(buf, y);
        remmy::Serialize(buf, z);
    }

    void Deserialize(remmy::StreamBuffer& buf) {
        remmy::Deserialize(buf, x);
        remmy::Deserialize(buf, y);
        remmy::Deserialize(buf, z);
    }
};

/**   
 * A RPC protocol defines the request, response and handler of the RPC call. 
 * This is used to demonstrate the usage of old Protocol-based interface.
 */
class RPC_Protocol : public ProtocolWithUID<UniqueId("RPC_Proto")> {
public:
    ComplexType req;
    size_t resp;

    virtual void MarshallRequest(StreamBuffer & buf) {
        req.Serialize(buf);
    }

    virtual void MarshallResponse(StreamBuffer & buf) {
        Serialize(buf, resp);
    }

    virtual void UnmarshallRequest(StreamBuffer & buf) {
        req.Deserialize(buf);
    }

    virtual void UnmarshallResponse(StreamBuffer & buf) {
        Deserialize(buf, resp);
    }

    virtual void HandleRequest(void *server) {
        // as a demonstration, this protocol returns req.x + req.y + req.z.size(),
        // and adds this value to server, which is just a std::atomic<size_t>
        std::atomic<size_t>* s = static_cast<std::atomic<size_t>*>(server);
        resp = req.x + (size_t)req.y + req.z.size();
        // note that the handler can be executed by multiple threads at the same time,
        // we need to make it thread-safe
        s->fetch_add(resp);
        REMMY_WARN("Server is now %lu", s->load());
    }
};

#if USE_ASIO
typedef remmy::CommAsio CommT;
typedef remmy::AsioEP EP;
#else
typedef remmy::CommZmq CommT;
typedef remmy::ZmqEP EP;
#endif

int main(int argc, char ** argv) {
    const uint64_t ADD_OP = UniqueId("add");
    const uint64_t MUL_OP = UniqueId("mul");
    // create a server
    int port = 4444;
    CommT comm("127.0.0.1", port);
    remmy::RPCStub<EP> rpc(&comm, 1);
    // Register protocols the server provides
    // Template parameters: Response type, Request Type1, Request Type2...
    // The UniqueId() function returns compile-time determined uint64_t given a string.
    // It is a convinient way of getting unique ids for different rpcs.
    rpc.RegisterAsyncHandler<ADD_OP, int, int>(
        [](int x, int y) { REMMY_LOG("Received ADD(%d, %d)", x, y); });
    rpc.RegisterSyncHandler<MUL_OP, int, int, int>(
        [](int x, int y) -> int { return x*y; });
    // now register with the protocol-based interface
    std::atomic<size_t> size(0);
    rpc.RegisterProtocol<RPC_Protocol>(&size);
    // now start serving
    rpc.StartServing();

    // now, create a client

    #if USE_ASIO
    AsioEP ep(asio::ip::address::from_string("127.0.0.1"), port);
    #else
    EP ep("127.0.0.1", port);
    #endif

    // test rpc calls
    for(int i = 0; i < 1000; i++) rpc.RpcCallAsync<ADD_OP>(ep, 1, 2);
    remmy::ErrorCode ec;
    for (int i = 0; i < 1024; i++) {
        int x = rand(), y = rand();
        int r = 0;
        ec = rpc.RpcCall<MUL_OP>(ep, 0, r, x, y);
        if (ec != remmy::ErrorCode::SUCCESS) {
            cout << "error occurred when making sync call: " << (int)ec << endl;
        }
        else {
            //cout << x << " * " << y << " = " << r << endl;
            REMMY_ASSERT(x * y == r, "wrong result!");
        }
    }

    // test with protocol-based interface
    RPC_Protocol proto;
    proto.req.x = 10;
    proto.req.y = 1.0;
    proto.req.z = "12345";
    ec = rpc.RpcCall(ep, proto);
    if (ec != remmy::ErrorCode::SUCCESS) {
        cout << "error occurred when making sync call: " << (int)ec << endl;
    }
    else {
        cout << "resp = " << proto.resp << endl;
    }

    proto.req.x = 3;
    ec = rpc.RpcCall(ep, proto);
    if (ec != remmy::ErrorCode::SUCCESS) {
        cout << "error occurred when making sync call: " << (int)ec << endl;
    }
    else {
        cout << "resp = " << proto.resp << endl;
    }
    return 0;
}
