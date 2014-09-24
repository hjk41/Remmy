#pragma once

#include <memory>
#include <string>
#include "message.h"

namespace TinyRPC
{

    enum class CommErrors
    {
        SUCCESS = 0,
        UNKNOWN = 1,
        CONNECTION_REFUSED = 2,
        CONNECTION_ABORTED = 3,
        SEND_ERROR = 4,
        RECEIVE_ERROR = 5
    };

    template<class EndPointT>
    class TinyCommBase
    {
    public:
        typedef Message<EndPointT> MessageType;
        typedef std::shared_ptr<MessageType> MessagePtr;

        TinyCommBase(){};
        virtual ~TinyCommBase(){};

        // start polling for messages
        virtual void start()=0;
        // send/receive
        virtual CommErrors send(const MessagePtr &) = 0;
        virtual MessagePtr recv() = 0;
        virtual void WakeReceivingThreadsForExit() = 0;
    };

    template<class EndPointT>
    const std::string EPToString(const EndPointT & ep)
    {
        return std::to_string(ep);
    }

};
