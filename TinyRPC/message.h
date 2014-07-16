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
private:
	EndPointT remote_addr_;
    StreamBuffer buffer_;
};


};