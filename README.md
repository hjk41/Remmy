tinyrpc
=======

tinyrpc is a simple RPC library. The communication layer can be implemented with any network library. 

Currently, we suport MPI and boost::asio.

Current implementation uses C++11 threads and locks, so you need a new compiler to compile the code.

Tested on both Linux and Windows (Visual Studio 2015).

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


Any questions or suggestions, feel free to ping me: chuntao.hong@gmail.com
