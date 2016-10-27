#pragma once

#include <list>
#include "streambuffer.h"
#include "tinydatatypes.h"

namespace TinyRPC
{

template<class EndPointT>
class Message
{
public:
    Message() : status_(TinyErrorCode::SUCCESS){}

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

    void set_status(const TinyErrorCode & status)
    {
        status_ = status;
    }

    TinyErrorCode & get_status()
    {
        return status_;
    }

private:
    EndPointT remote_addr_;
    StreamBuffer buffer_;
    TinyErrorCode status_;  // indicating status of communication, success or fail
};


};