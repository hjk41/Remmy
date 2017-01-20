#pragma once

#include <list>
#include "streambuffer.h"
#include "tinydatatypes.h"

namespace tinyrpc {

template<class EndPointT>
class Message {
public:
    Message() : status_(TinyErrorCode::SUCCESS){}

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

    void SetStatus(const TinyErrorCode & status) {
        status_ = status;
    }

    const TinyErrorCode & GetStatus() const {
        return status_;
    }

private:
    EndPointT remote_addr_;
    StreamBuffer buffer_;
    TinyErrorCode status_;  // indicating status of communication, success or fail
};

};