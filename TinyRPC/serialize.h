#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include "logging.h"
#include "streambuffer.h"

namespace tinyrpc {

#ifdef _WIN32
    template<typename T>
    struct TriviallyCopyable {
        static const bool value = std::is_trivially_copyable<T>::value;
    };
#else
    template<typename T>
    struct TriviallyCopyable {
        static const bool value = std::has_trivial_copy_constructor<T>::value;
    };
#endif

    template<typename T, bool Enable = TriviallyCopyable<T>::value>
    class Serializer {
        // If you get "unresolved external symbol" error, it means you 
        // have tried to Serialize a non-trivially-copyable class, and
        // you haven't specialize a Serialize function for it.
        // Please do it like this:
        //
        // template<>
        // void TinyRPC::Serialize<MyType>(TinyRPC::StreamBuffer & buf, const MyType & v)
        // {
        //      buf.write(&(v.xxx), sizeof(v.xxx));
        //      buf.write(&(v.yyy), sizeof(v.yyy));
        // }
        //
        // The same works with Deserialize. Remember to declare this function
        // as a friend of class MyType, if you want to access private members
        // of MyType.
    public:
        static void Serialize(StreamBuffer &, const T &);
        static void Deserialize(StreamBuffer &, T &);
    };

    template<typename T>
    class Serializer <T, true> {
    public:
        static void Serialize(StreamBuffer & buf, const T & val) {
            buf.Write(&val, sizeof(T));
        }
        static void Deserialize(StreamBuffer & buf, T & val) {
            buf.Read(&val, sizeof(T));
        }
    };

    // partial specialization for pair
    template<typename T1, typename T2>
    class Serializer <std::pair<T1, T2>, false> {
    public:
        static void Serialize(StreamBuffer & buf, const std::pair<T1, T2> & val) {
            Serialize(buf, val.first);
            Serialize(buf, val.second);
        }
        static void Deserialize(StreamBuffer & buf, std::pair<T1, T2> & val) {
            Deserialize(buf, val.first);
            Deserialize(buf, val.second);
        }
    };

    // partial specialization for map
    template<typename K, typename V>
    class Serializer <typename std::map<K, V>, false> {
    public:
        static void Serialize(StreamBuffer & buf, const std::map<K, V> & m) {
            buf.Write(m.size());
            for (auto & kv : m) {
                Serialize(buf, kv.first);
                Serialize(buf, kv.second);
            }
        }

        static void Deserialize(StreamBuffer & buf, std::map<K, V> & m) {
            size_t size;
            buf.Read(size);
            for (size_t i = 0; i < size; i++) {
                std::pair<K, V> p;
                Deserialize(buf, p.first);
                Deserialize(buf, p.second);
                m.insert(m.end(), p);
            }
        }
    };

    // Trivially copyable classes can be handled directly
    template<typename T>
    void Serialize(StreamBuffer & buf, const T & val) {
        Serializer<T>::Serialize(buf, val);
    }

    template<class T>
    void Deserialize(StreamBuffer & buf, T & val) {
        Serializer<T>::Deserialize(buf, val);
    }
    
    // ------------------------------
    // specially for vector
    // If T is not trivially copyable, we must copy them one-by-one
    // If T is trivially copyable, we copy the whole vector at once
    template<typename T, bool Enable = TriviallyCopyable<T>::value>
    class VectorSerializer {
    public:
        static void Serialize(StreamBuffer & buf, const std::vector<T> & vec) {
            buf.Write(vec.size());
            for (auto & iter : vec) {
                Serialize<T>(buf, iter);
            }
        }
        static void Deserialize(StreamBuffer & buf, std::vector<T> & vec) {
            size_t size;
            buf.Read(size);
            vec.resize(size);
            for (auto & iter : vec) {
                Deserialize<T>(buf, iter);
            }             
        }
    };

    template<typename T>
    class VectorSerializer <T, true> {
    public:
        static void Serialize(StreamBuffer & buf, const std::vector<T> & vec) {
            buf.Write(vec.size());
            if (!vec.empty()) {
                buf.Write(&vec[0], sizeof(T)*vec.size());
            }
        }
        static void Deserialize(StreamBuffer & buf, std::vector<T> & vec) {
            size_t size;
            buf.Read(size);
            vec.resize(size);
            if (!vec.empty()) {
                buf.Read(&vec[0], sizeof(T)*size);
            }
        }
    };

    template<typename T>
    void Serialize(StreamBuffer & buf, const std::vector<T> & vec) {
        VectorSerializer<T>::Serialize(buf, vec);
    }  

    template<typename T>
    void Deserialize(StreamBuffer & buf, std::vector<T> & vec) {
        VectorSerializer<T>::Deserialize(buf, vec);
    }

    template<typename T>
    void Serialize(StreamBuffer & buf, const std::unordered_set<T> & set) {
        buf.Write(set.size());
        for (auto & iter : set) {
            Serialize<T>(buf, iter);
        }
    }

    template<typename T>
    void Deserialize(StreamBuffer & buf, std::unordered_set<T> & set) {
        size_t size;
        for (buf.Read(size); size; --size) {
            T value;
            Deserialize<T>(buf, value);
            set.insert(value);
        }
    }

    template<>
    inline void Serialize<std::string>(StreamBuffer & buf, const std::string & str) {
        buf.Write(str.size());
        buf.Write(str.c_str(),  str.size());
    }

    template<>
    inline void Deserialize<std::string>(StreamBuffer & buf, std::string & str) {
        size_t size;
        buf.Read(size);
        str.resize(size);
        if (!str.empty()){
            buf.Read(&str[0], size);
        }
    }
}
