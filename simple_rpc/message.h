#pragma once

#include <list>
#include "streambuffer.h"
#include "datatypes.h"

namespace simple_rpc {

template<class EndPointT>
class Message {
public:
    Message() : status_(ErrorCode::SUCCESS){}

    void SetRemoteAddr(const EndPointT & addr) {
        remote_addr_ = addr;
    }

    void SetRemoteAddr(EndPointT&& addr) {
        remote_addr_ = addr;
    }

    const EndPointT & GetRemoteAddr() const {
        return remote_addr_;
    }

    StreamBuffer & GetStreamBuffer() {
        return buffer_;
    }

    void SetStreamBuffer(StreamBuffer & buf) {
        buffer_.Swap(buf);
    }

    void SetStatus(const ErrorCode & status) {
        status_ = status;
    }

    const ErrorCode & GetStatus() const {
        return status_;
    }

private:
    EndPointT remote_addr_;
    StreamBuffer buffer_;
    ErrorCode status_;  // indicating status of communication, success or fail
};

};