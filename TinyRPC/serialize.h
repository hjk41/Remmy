#pragma once

#include <vector>
#include "logging.h"
#include "streambuffer.h"

namespace TinyRPC
{
    template<class T>
    void Serialize(StreamBuffer & buf, const T & val)
    {
        ASSERT(std::is_pod<T>::value, "Serialize function for %s is not implemented.", typeid(T).name());
        //static_assert(std::is_pod<T>::value, "Serialize function for this type is not implemented.");
        buf.write(val);
    }
    
    template<class T>
    void Deserialize(StreamBuffer & buf, T & val)
    {
        ASSERT(std::is_pod<T>::value, "Deserialize function for %s is not implemented.", typeid(T).name());
        buf.read(val);
    }
    
    template<class T>
    void Serialize(StreamBuffer & buf, const std::vector<T> & vec)
    {
        ASSERT(std::is_pod<T>::value, "Serialize function for %s is not implemented.", typeid(T).name());
        buf.write(vec.size());
        buf.write(&vec[0], sizeof(T)*vec.size());
    }

    template<class T>
    void Deserialize(StreamBuffer & buf, std::vector<T> & vec)
    {
        ASSERT(std::is_pod<T>::value, "Deserialize function for %s is not implemented.", typeid(T).name());
        size_t size;
        buf.read(size);
        vec.resize(size);
        buf.read(&vec[0], sizeof(T)*vec.size());
    }
}