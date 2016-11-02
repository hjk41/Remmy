#pragma once

#include <cstdint>
#include <iostream>
#include "streambuffer.h"
#include "Serialize.h"

namespace tinyrpc {
    class ProtocolBase {
    public:
        virtual uint32_t UniqueId() = 0;
        
        virtual void MarshallRequest(StreamBuffer &) = 0;

        virtual void MarshallResponse(StreamBuffer &) = 0;

        virtual void UnmarshallRequest(StreamBuffer &) = 0;

        virtual void UnmarshallResponse(StreamBuffer &) = 0;

        virtual void HandleRequest(void *server) = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase {
    public:
        RequestT request;
        ResponseT response;

        virtual void MarshallRequest(StreamBuffer & buf) override {
            Serialize(buf, request);
        }

        void MarshallResponse(StreamBuffer & buf) override {
            Serialize(buf, response);
        }

        virtual void UnmarshallRequest(StreamBuffer & buf) override {
            Deserialize(buf, request);
        }

        virtual void UnmarshallResponse(StreamBuffer & buf) override {
            Deserialize(buf, response);
        }
    };

};