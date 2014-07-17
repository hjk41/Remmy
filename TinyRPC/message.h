#pragma once

#include <list>
#include "streambuffer.h"

namespace TinyRPC
{


template<class EndPointT>
class Message
{
public:
	void set_remote_addr(const EndPointT & addr)
    {
        remote_addr_ = addr;
    }

	EndPointT & get_remote_addr()
    {
        return remote_addr_;
    }

    StreamBuffer & get_stream_buffer()
    {
        return buffer_;
    }

    void set_stream_buffer(StreamBuffer & buf)
    {
        buffer_.swap(buf);
    }

	int64_t get_seq() {
		return seq_;
	}

	void set_seq(int64_t seq) {
		seq_ = seq;
	}

	uint32_t get_protocol_id() {
		return protocol_id_;
	}

	void set_protocol_id(uint32_t protocol_id) {
		protocol_id_ = protocol_id;
	}

	uint32_t get_sync() {
		return sync_;
	}

	void set_sync(uint32_t sync) {
		sync_ = sync;
	}

private:
	EndPointT remote_addr_;
    StreamBuffer buffer_;
	int64_t seq_;
	uint32_t protocol_id_, sync_;
};


};