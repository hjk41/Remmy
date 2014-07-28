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

		virtual void marshall_request() = 0;

		virtual void marshall_response() = 0;

		virtual void unmarshall_request(StreamBuffer & buf) = 0;

		virtual void unmarshall_response(StreamBuffer & buf) = 0;

		virtual void handle_request(void *server) = 0;

		StreamBuffer buf_;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase
    {
		static_assert(std::is_pod<RequestT>::value && std::is_pod<ResponseT>::value, "Protocol is not implemented for this type.");
    public:
		RequestT request;
		ResponseT response;

		void marshall_request() {
			buf_.clear(true);
			buf_.write(request);
		}

		void marshall_response() {
			buf_.clear(true);
			buf_.write(response);
		}

		void unmarshall_request(StreamBuffer & buf) {
			buf.read(request);
		}

		void unmarshall_response(StreamBuffer & buf) {
			buf.read(response);
		}
    };

};