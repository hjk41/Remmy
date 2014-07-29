#pragma once

#include <cstdint>
#include <iostream>
#include "streambuffer.h"
#include "serialize.h"

namespace TinyRPC
{
    class ProtocolBase
    {
    public:
        virtual uint32_t get_id() = 0;
		
		virtual StreamBuffer & marshall_request(StreamBuffer &) = 0;

        virtual StreamBuffer & marshall_response(StreamBuffer &) = 0;

        virtual StreamBuffer & unmarshall_request(StreamBuffer &) = 0;

        virtual StreamBuffer & unmarshall_response(StreamBuffer &) = 0;

		virtual void handle_request(void *server) = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
        static_assert(std::is_pod<RequestT>::value, "RequestT must be POD.");
        static_assert(std::is_pod<ResponseT>::value, "ResponseT must be POD.");
    public:
		RequestT request;
		ResponseT response;

        virtual StreamBuffer & marshall_request(StreamBuffer & buf) override
        {
            //return Serialize(buf, request);
            return buf;
        }

        StreamBuffer & marshall_response(StreamBuffer & buf) override
        {
            //return Serialize(buf, response);
            return buf;
		}

        virtual StreamBuffer & unmarshall_request(StreamBuffer & buf) override
        {
            return Deserialize(buf, request);
        }

        virtual StreamBuffer & unmarshall_response(StreamBuffer & buf) override
        {
            return Deserialize(buf, response);
        }
    };

};