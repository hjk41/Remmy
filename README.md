tinyrpc
=======

tinyrpc is a simple RPC library. The communication layer can be implemented with any network library. 

Currently, we suport ASIO and ZeroMQ as the network layer.

Current implementation uses C++14, so you need a new compiler to compile the code.

Tested on both Linux and Windows (Visual Studio 2017).

Programming interface
=======

```c++
    // start server
    // ...
    rpc.RegisterProtocol<int, int, int>(UniqueId("add"), 
        [](int x, int y) { return x + y; });
    rpc.StartServing();

    // now, create a client
    AsioEP ep(asio::ip::address::from_string("127.0.0.1"), port);
    int result;
    rpc.RpcCall(ep, UniqueId("add"), result, 1, 2);
    std::cout << "1 + 2 = " << result << std::endl;
```

Please refer to [/test/test.cpp](/test/test.cpp) for an example on how to use tinyrpc.
