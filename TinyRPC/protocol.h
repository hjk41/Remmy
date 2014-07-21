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
		
		StreamBuffer & get_buf() {
			return buf_;
		}

		virtual void get_response(StreamBuffer & buf) = 0;

		virtual void handle_request(StreamBuffer & buf) = 0;

		StreamBuffer buf_;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
        static_assert(std::is_pod<RequestT>::value && std::is_pod<ResponseT>::value);
    public:
		RequestT request;
		ResponseT response;

		void set_request(RequestT &req) {
			buf_.clear();
			buf_.write(req);
		}
		
		void get_response(StreamBuffer & buf) {
			buf.read(response);
		}
    };

};