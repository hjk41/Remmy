#pragma once

#include <list>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
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

    #define HAS_MEM_FUNC(func, name)                                        \
    template<typename T, typename Sign>                                 \
    struct name {                                                       \
        typedef char yes[1];                                            \
        typedef char no [2];                                            \
        template <typename U, U> struct type_check;                     \
        template <typename _1> static yes &chk(type_check<Sign, &_1::func > *); \
        template <typename   > static no  &chk(...);                    \
        static bool const value = sizeof(chk<T>(0)) == sizeof(yes);     \
    }

    HAS_MEM_FUNC(Serialize, _has_serialize_);
    HAS_MEM_FUNC(Deserialize, _has_deserialize_);

    template<typename T, bool isClass = std::is_class<T>::value>
    struct _has_serialize {
        const static bool value =
            (_has_serialize_<T, void(T::*)(StreamBuffer&)const>::value ||
                _has_serialize_<T, void(T::*)(StreamBuffer&)>::value) &&
            _has_deserialize_<T, void(T::*)(StreamBuffer&)>::value;
    };

    template<typename T>
    struct _has_serialize<T, false> {
        const static bool value = false;
    };

    #define ENABLE_IF_HAS_SERIALIZE(T, RT) typename std::enable_if<_has_serialize<T>::value, RT>::type

    template<typename T>
    struct _should_do_memcpy {
        const static bool value = TriviallyCopyable<T>::value
            && !std::is_pointer<T>::value
            && !_has_serialize<T>::value;
    };

    template<typename T>
    class Serializer {
    public:
        /*
        * If T has Serialize and Deserialize, then use them
        */
        template<typename T2 = T>
        static typename std::enable_if<_has_serialize<T2>::value, void>::type 
            Serialize(StreamBuffer& buf, const T2& d) {
            d.Serialize(buf);
        }

        template<typename T2 = T>
        static typename std::enable_if<_has_serialize<T2>::value, void>::type
            Deserialize(StreamBuffer& buf, T2& d) {
            d.Deserialize(buf);
        }

        /*
        * Otherwise, use memcpy if applicable
        */
        template<typename T2 = T>
        static typename std::enable_if<_should_do_memcpy<T2>::value, void>::type
            Serialize(StreamBuffer & buf, const T2 & val) {
            buf.Write(&val, sizeof(T));
        }

        template<typename T2 = T>
        static typename std::enable_if<_should_do_memcpy<T2>::value, void>::type
            Deserialize(StreamBuffer & buf, T2 & val) {
            buf.Read(&val, sizeof(T));
        }

        template <bool v, typename T_>
        struct _AssertValue
        {
            static_assert(v, "Assertion failed <see below for more information>");
            static bool const value = v;
        };
        
        /*
        * Else, raise an error
        */
        template<typename T2 = T>
        static typename std::enable_if<!_has_serialize<T2>::value && !_should_do_memcpy<T2>::value, void>::type
            Serialize(StreamBuffer & buf, const T2 & val) {
            static_assert(_AssertValue<false, T2>::value,
                "Serialize not defined for this type. You can define it by:\n"
                "    1. define void Serialize(Streambuf&, const T&), or\n"
                "    2. define T::Serialize(Streambuf&), or\n"
                "    3. define Serializer<T>::Serialize(StreamBuf&, const T&)");
        }

        template<typename T2 = T>
        static typename std::enable_if<!_has_serialize<T2>::value && !_should_do_memcpy<T2>::value, void>::type
            Deserialize(StreamBuffer & buf, T2 & val) {
            static_assert(_AssertValue<false, T2>::value,
                "Deserialize not defined for this type. You can define it by:\n"
                "    1. define void Deserialize(Streambuf&, T&), or\n"
                "    2. define T::Deserialize(Streambuf&), or\n"
                "    3. define Serializer<T>::Deserialize(StreamBuf&, T&)");
        }
    };

    // partial specialization for pair
    template<typename T1, typename T2>
    class Serializer <std::pair<T1, T2>> {
    public:
        static void Serialize(StreamBuffer & buf, const std::pair<T1, T2> & val) {
            Serializer<T1>::Serialize(buf, val.first);
            Serializer<T2>::Serialize(buf, val.second);
        }
        static void Deserialize(StreamBuffer & buf, std::pair<T1, T2> & val) {
            Serializer<T1>::Deserialize(buf, val.first);
            Serializer<T2>::Deserialize(buf, val.second);
        }
    };

    template<typename T>
    inline void Serialize(StreamBuffer & buf, const T & val) {
        Serializer<T>::Serialize(buf, val);
    }

    template<class T>
    inline void Deserialize(StreamBuffer & buf, T & val) {
        Serializer<T>::Deserialize(buf, val);
    }

    // ------------------------------
    // specially for vector
    // If T is not trivially copyable, we must copy them one-by-one
    // If T is trivially copyable, we copy the whole vector at once
    template<typename ContainerT, typename T, 
        bool ONE_BY_ONE = true>
    class ContainerSerializer {
    public:
        static void Serialize(StreamBuffer& buf, const ContainerT& vec) {
            size_t size = vec.size();
            buf.Write(&size, sizeof(size));
            for (auto & iter : vec) {
                tinyrpc::Serialize(buf, iter);
            }
        }
        static void Deserialize(StreamBuffer& buf, ContainerT& vec) {
            size_t size;
            buf.Read(&size, sizeof(size));
            vec.resize(size);
            for (auto & iter : vec) {
                tinyrpc::Deserialize(buf, iter);
            }             
        }
    };

    template<typename ContainerT, typename T>
    class ContainerSerializer <ContainerT, T, false> {
    public:
        static void Serialize(StreamBuffer& buf, const ContainerT& vec) {
            size_t size = vec.size();
            buf.Write(&size, sizeof(size));
            if (!vec.empty()) {
                buf.Write(&vec[0], sizeof(T)*vec.size());
            }
        }
        static void Deserialize(StreamBuffer& buf, ContainerT& vec) {
            size_t size;
            buf.Read(&size, sizeof(size));
            vec.resize(size);
            if (!vec.empty()) {
                buf.Read(&vec[0], sizeof(T)*size);
            }
        }
    };

    template<typename T>
    class Serializer<std::vector<T>>
        : public ContainerSerializer<std::vector<T>, T, !_should_do_memcpy<T>::value> {};

    template<>
    class Serializer<std::string>
        : public ContainerSerializer<std::string, char, false> {};

    template<typename T>
    class Serializer<std::deque<T>>
        : public ContainerSerializer<std::deque<T>, T, true> {};

    template<typename T>
    class Serializer<std::list<T>>
        : public ContainerSerializer<std::list<T>, T, true> {};

    template<typename T>
    class Serializer<std::set<T>> : public ContainerSerializer<std::set<T>, T, true> {};

    template<typename T>
    class Serializer<std::unordered_set<T>> : public ContainerSerializer<std::unordered_set<T>, T, true> {};

    template<typename K, typename V>
    class Serializer<std::map<K, V>> : public ContainerSerializer<std::map<K, V>, std::pair<K, V>, true> {};

    template<typename K, typename V>
    class Serializer<std::unordered_map<K, V>> : public ContainerSerializer<std::unordered_map<K, V>, std::pair<K, V>, true> {};

    
    template<typename T>
    inline void SerializeVariadic(StreamBuffer& buf, const T& d) {
        Serialize(buf, d);
    }

    template<typename T, typename... Ts>
    inline void SerializeVariadic(StreamBuffer& buf, const T& d, const Ts&... dd) {
        Serialize(buf, d);
        SerializeVariadic(buf, dd...);
    }

    namespace _detail {
        template<typename Tup, size_t N>
        struct TupleDeserializer {            
            static inline void Apply(StreamBuffer& buf, Tup& tup) {
                auto& e = std::get<std::tuple_size<Tup>::value - N>(tup);
                Deserialize(buf, e);
                TupleDeserializer<Tup, N - 1>::Apply(buf, tup);
            }
        };

        template<typename Tup>
        struct TupleDeserializer<Tup, 0> {
            static inline void Apply(StreamBuffer&, Tup&) {}
        };

        template<typename Tup, size_t N>
        struct TupleSerializer {
            static inline void Apply(StreamBuffer& buf, const Tup& tup) {
                auto& e = std::get<std::tuple_size<Tup>::value - N>(tup);
                Serialize(buf, e);
                TupleSerializer<Tup, N - 1>::Apply(buf, tup);
            }
        };

        template<typename Tup>
        struct TupleSerializer<Tup, 0> {
            static inline void Apply(StreamBuffer& buf, const Tup& tup) {}
        };
    }

    template<typename... Ts>
    inline void DeserializeVariadic(StreamBuffer& buf, std::tuple<Ts...>& tup) {
        using Tup = std::tuple<Ts...>;
        _detail::TupleDeserializer<Tup, std::tuple_size<Tup>::value>::Apply(buf, tup);
    }

    template<typename... Ts>
    inline void Serialize(StreamBuffer& buf, const std::tuple<Ts...>& tup) {
        return _detail::TupleSerializer<std::tuple<Ts...>,
            std::tuple_size<std::tuple<Ts...>>::value>::Apply(buf, tup);
    }

    template<typename... Ts>
    inline void Deserialize(StreamBuffer& buf, std::tuple<Ts...>& tup) {
        return _detail::TupleDeserializer<std::tuple<Ts...>,
            std::tuple_size<std::tuple<Ts...>>::value>::Apply(buf, tup);
    }
}
