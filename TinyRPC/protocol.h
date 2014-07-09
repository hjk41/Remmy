#pragma once

#include <cstdint>
#include <iostream>
#include "serialize.h"

namespace TinyRPC
{
    class ProtocolBase
    {
    public:
        virtual std::ostream & marshall_request(std::ostream & os) = 0;
        virtual std::istream & unmarshall_request(std::istream & is) = 0;
        virtual void handle_request(void * server) = 0;
        virtual std::ostream & marshall_response(std::ostream & os) = 0;
        virtual std::istream & unmarshall_response(std::istream & is) = 0;
        virtual uint32_t ID() = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
    public:
        virtual std::ostream & marshall_request(std::ostream & os)
        {
            Marshall(os, request);
            return os;
        };
        virtual std::istream & unmarshall_request(std::istream & is)
        {
            UnMarshall(is, request);
            return is;
        };
        virtual std::ostream & marshall_response(std::ostream & os)
        {
            Marshall(os, response);
            return os;
        }
        virtual std::istream & unmarshall_response(std::istream & is)
        {
            UnMarshall(is, response);
            return is;
        }
    public:
        RequestT request;
        ResponseT response;
    };

};