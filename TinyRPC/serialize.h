#pragma once

#include "logging.h"
#include "streambuffer.h"

namespace TinyRPC
{

    template<class T>
    StreamBuffer & Serialize(StreamBuffer & buf, const T & val)
    {
        ASSERT(std::is_pod<T>::value, "Serialize function for %s is not implemented.", typeid(T).name());
        buf.write(val);
        return buf;
    }

    template<class T>
    StreamBuffer & Deserialize(StreamBuffer & buf, T & val)
    {
        ASSERT(std::is_pod<T>::value, "Deserialize function for %s is not implemented.", typeid(T).name());
        buf.read(val);
        return buf;
    }
}