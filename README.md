SimpleRPC
=======

SimpleRPC is a simple but usable RPC library. Thanks to the structual simplicity of the code, it is sutable for use in education as well.

The communication layer can be implemented with any network library. Currently, we suport ASIO and ZeroMQ as the network layer.

Current implementation uses C++14, so you need a new compiler to compile the code.

Tested on both Linux and Windows (Visual Studio 2017).

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
        SIMPLE_WARN("Server is now %lu", s->load());
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

Please refer to [/test/test.cpp](/test/test.cpp) for an example on how to use SimpleRPC.

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
