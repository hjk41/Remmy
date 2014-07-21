#pragma once

#include <memory>
#include "message.h"

namespace TinyRPC
{

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
    virtual void send(const MessagePtr &) = 0;
    virtual MessagePtr recv() = 0;
};

};
