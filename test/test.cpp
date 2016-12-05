#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
using namespace std;

#include "comm_asio.h"
#include "message.h"
#include "streambuffer.h"
#include "tinyrpc.h"
#include "unique_id.h"
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

int main(int argc, char ** argv) {
    // create a server
    int port = 4444;
    tinyrpc::TinyCommAsio comm(port);
    tinyrpc::TinyRPCStub<AsioEP> rpc(&comm);
    // Register protocols the server provides
    // Template parameters: Response type, Request Type1, Request Type2...
    // The UniqueId() function returns compile-time determined uint64_t given a string.
    // It is a convinient way of getting unique ids for different rpcs.
    rpc.RegisterProtocol<int, int, int>(UniqueId("add"), 
        [](int x, int y) { return x + y; });
    rpc.RegisterProtocol<int, int, int>(UniqueId("min"), 
        [](int x, int y) { return x - y; });
    rpc.RegisterProtocol<int, int, int>(UniqueId("mul"), 
        [](int x, int y) { return x * y; });
    rpc.RegisterProtocol<int, int, int>(UniqueId("div"), 
        [](int x, int y) { return x / y; });
    rpc.RegisterProtocol<ComplexType, ComplexType>(UniqueId("foo"),
        [](ComplexType req) {
        req.x += 10;
        req.y += 20;
        req.z += " world";
        return req;
    });
    // now start serving
    rpc.StartServing();

    // now, create a client
    AsioEP ep(asio::ip::address::from_string("127.0.0.1"), port);
    int result;
    rpc.RpcCall(ep, UniqueId("add"), result, 1, 2);
    std::cout << "1 + 2 = " << result << std::endl;
    rpc.RpcCall(ep, UniqueId("min"), result, 100, 66);
    std::cout << "100 - 66 = " << result << std::endl;
    rpc.RpcCall(ep, UniqueId("mul"), result, 11, 7);
    std::cout << "11 * 7 = " << result << std::endl;
    rpc.RpcCall(ep, UniqueId("div"), result, 100, 6);
    std::cout << "100 / 6 = " << result << std::endl;
    ComplexType req;
    req.x = 11;
    req.y = 22;
    req.z = "hello";
    ComplexType resp;
    rpc.RpcCall(ep, UniqueId("foo"), resp, req);
    std::cout << "complex response: x=" << resp.x
        << " y=" << resp.y
        << " z=\"" << resp.z << "\"" << std::endl;
    return 0;
}
