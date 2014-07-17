#pragma once

#include <cstdint>
#include <iostream>
#include "streambuffer.h"

namespace TinyRPC
{
    class ProtocolBase
    {
		StreamBuffer buf_;
    public:
        virtual uint32_t get_id() = 0;
		StreamBuffer set_buf(StreamBuffer & buf) {
			buf_.swap(buf);
		}
		StreamBuffer & get_buf() {
			return buf_;
		}
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
    public:
        RequestT request;
        ResponseT response;
    };

};