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
		
		void set_buf(StreamBuffer & buf) {
			buf_.swap(buf);
		}
		
		StreamBuffer & get_buf() {
			return buf_;
		}

		virtual void set_request(StreamBuffer & buf) = 0;

		virtual void get_response(StreamBuffer & buf) = 0;

		virtual void handle_request(StreamBuffer & buf) = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
    public:
		RequestT request;
		ResponseT response;

		virtual void set_request(StreamBuffer & buf) {
			set_buf(buf);
		}
		
		virtual void get_response(StreamBuffer & buf) {
			buf.read(response);
		}
    };

};