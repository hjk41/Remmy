#pragma once

#include <cstdint>
#include <iostream>
#include "streambuffer.h"
#include "serialize.h"

namespace tinyrpc
{
    class ProtocolBase
    {
    public:
        virtual uint32_t get_id() = 0;
        
        virtual void marshall_request(StreamBuffer &) = 0;

        virtual void marshall_response(StreamBuffer &) = 0;

        virtual void unmarshall_request(StreamBuffer &) = 0;

        virtual void unmarshall_response(StreamBuffer &) = 0;

        virtual void handle_request(void *server) = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
    public:
        RequestT request;
        ResponseT response;

        virtual void marshall_request(StreamBuffer & buf) override
        {
            Serialize(buf, request);
        }

        void marshall_response(StreamBuffer & buf) override
        {
            Serialize(buf, response);
        }

        virtual void unmarshall_request(StreamBuffer & buf) override
        {
            Deserialize(buf, request);
        }

        virtual void unmarshall_response(StreamBuffer & buf) override
        {
            Deserialize(buf, response);
        }
    };

};