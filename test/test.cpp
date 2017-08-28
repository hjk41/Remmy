#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
using namespace std;

#include "comm_zmq.h"
#include "message.h"
#include "streambuffer.h"
#include "tinyrpc.h"
#include "unique_id.h"
#include "comm_asio.h"
using namespace tinyrpc;

struct ComplexType {
    int x;
    double y;
    std::string z;

    void Serialize(StreamBuffer& buf) const {
        tinyrpc::Serialize(buf, x);
        tinyrpc::Serialize(buf, y);
        tinyrpc::Serialize(buf, z);
    }

    void Deserialize(tinyrpc::StreamBuffer& buf) {
        tinyrpc::Deserialize(buf, x);
        tinyrpc::Deserialize(buf, y);
        tinyrpc::Deserialize(buf, z);
    }
};

#if USE_ASIO
typedef tinyrpc::TinyCommAsio CommT;
typedef tinyrpc::AsioEP EP;
#else
typedef tinyrpc::TinyCommZmq CommT;
typedef tinyrpc::ZmqEP EP;
#endif

int main(int argc, char ** argv) {
    const uint64_t ADD_OP = UniqueId("add");
    const uint64_t MUL_OP = UniqueId("mul");
    // create a server
    int port = 4444;
    CommT comm("127.0.0.1", port);
    tinyrpc::TinyRPCStub<EP> rpc(&comm, 1);
    // Register protocols the server provides
    // Template parameters: Response type, Request Type1, Request Type2...
    // The UniqueId() function returns compile-time determined uint64_t given a string.
    // It is a convinient way of getting unique ids for different rpcs.
    rpc.RegisterAsyncHandler<ADD_OP, int, int>(
        [](int x, int y) { cout << x << "+" << y << "=" << x + y << endl; });
    rpc.RegisterSyncHandler<MUL_OP, int, int, int>(
        [](int x, int y) -> int { return x*y; });
    // now start serving
    rpc.StartServing();

    // now, create a client

    #if USE_ASIO
    AsioEP ep(asio::ip::address::from_string("127.0.0.1"), port);
    #else
    EP ep("127.0.0.1", port);
    #endif

    rpc.RpcCallAsync<ADD_OP>(ep, 1, 2);
    int x = 2, y = 3;
    int r = 0;
    auto ec = rpc.RpcCall<MUL_OP>(ep, 0, r, x, y);
    if (ec != tinyrpc::TinyErrorCode::SUCCESS) {
        cout << "error occurred when making sync call: " << (int)ec << endl;
    }
    else {
        cout << x << "*" << y << "=" << r << endl;
    }

    return 0;
}
