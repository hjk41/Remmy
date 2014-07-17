#pragma once

#include <cstdint>
#include <iostream>
#include "streambuffer.h"

namespace TinyRPC
{
    class ProtocolBase
    {
    public:
        virtual uint32_t get_id() = 0;
		virtual StreamBuffer get_buf() = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
    public:
        RequestT request;
        ResponseT response;
    };

};