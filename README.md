Remmy
=======

Remmy is a simple but usable RPC library. Thanks to the structural simplicity of the code, it is suitable for use in education as well.

The communication layer can be implemented with any network library. Currently, we support ASIO and ZeroMQ as the network layer.

Current implementation uses C++14, so you need a new compiler to compile the code.

Tested on both Linux and Windows (Visual Studio 2019).


Compiling Test
=======

test/test.cpp is a simple test demonstrating how to use Remmy. You can compile it with `CMake`, `make`, or `Visual Studio`.

**Compiling With CMake**

To compile with `CMake`, use the following command:
```bash
user@myhost:~/projects/Remmy$ mkdir build
user@myhost:~/projects/Remmy$ cd build
user@myhost:~/projects/Remmy/build$ cmake .. -DCOMM_LAYER=ASIO
-- The C compiler identification is GNU 7.4.0
-- The CXX compiler identification is GNU 7.4.0
-- ...
-- Configuring done
-- Generating done
-- Build files have been written to: /home/user/projects/Remmy/build
user@myhost:~/projects/Remmy/build$ make
Scanning dependencies of target remmy_test
[ 50%] Building CXX object CMakeFiles/remmy_test.dir/test/test.cpp.o
[100%] Linking CXX executable remmy_test
[100%] Built target remmy_test
```

Note that we use `-DCOMM_LAYER=ASIO` option for `CMake` here. Remmy supports ASIO and ZMQ. Here we choose ASIO as the communication layer.

If you want to use ZeroMQ, you also need to install libzmq.

**Compiling With `make`**

There is a Makefile under `Remmy/test`, which can be used to compile the test. The communication can be switched by defining the `USE_ASIO` or `USE_ZMQ` at the top of the Makefile.

**Building With VS 2017/2019**

`Remmy/remmy.sln` is a VS 2019 solution file. You can use VS 2019 to open the solution and compile the test project. However, if you choose to use VS 2017, you need to retarget the solution before you can build it. To retarget the solution, open it and right-click on the `Solution 'Remmy'` in `Solution Explorer`, then choose `Retarget Solution`. In the pop-up windows, choose `latest installed version` for `Windows SDK Version`.

By default the solution uses ASIO as the communication layer. Switching to ZeroMQ requires defining `USE_ZMQ` and `USE_ASIO` macros in project properties. You will also need to specify the location to ZeroMQ library when using ZeroMQ.


Programming interface
=======

```c++
// define protocol
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

    // start server
    // ...
    std::atomic<size_t> size = 0;
    rpc.RegisterProtocol<RPC_Protocol>(&size);
    rpc.StartServing();

    // now, create a client
    AsioEP ep(asio::ip::address::from_string("127.0.0.1"), port);
    RPC_Protocol proto;
    proto.req.x = 10;
    proto.req.y = 1.0;
    proto.req.z = "12345";
    ec = rpc.RpcCall(ep, proto);
    std::cout << "response = " << proto.resp << std::endl;
```

Please refer to [/test/test.cpp](/test/test.cpp) for an example on how to use Remmy.


Contributing
=======
Everyone is welcome to contribute to this project, either to improve the code or documentation.

The whole library can be divided into these parts:

* rpc_stub.h: contains RPCStub class, which is the entry point

* protocol.h: declares the interface of protocols

* serialize.h: implements the Serializer template class

* comm.h: declares the communication layer, which can be implemented with any network libray, such as asio (as in comm_asio.h), ZeroMQ, etc.

* other stuff: incluing logging, message structure, buffer, concurrent queue, unique id, ...

I will try to write as much document as possible in the files, but you are also welcome to contribute standalone document files.
