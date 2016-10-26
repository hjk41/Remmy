#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include "logging.h"

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "StreamBuffer"

namespace TinyRPC
{
    class StreamBuffer
    {
        const static bool SHRINK_WITH_GET = false;
        const static size_t GROW_SIZE = 1024;
        const static size_t RESERVED_HEADER_SPACE = 64;
    public:
        /// <summary>
        /// Since we might further push some header information such as message ID
        /// into this buffer, we would like to reserve some space for the header info.
        /// Here we allocate 128 bytes and reserve the first 64 bytes as header space.
        /// </summary>
        StreamBuffer()
            : buf_(nullptr),
            const_buf_(false),
            pend_(0),
            gpos_(0),
            ppos_(0)
        {
        }

        void init_ostream()
        {
            ASSERT(buf_ == nullptr, "trying to init a already-initialized buffer");
            buf_ = (char*)malloc(RESERVED_HEADER_SPACE * 2);
            const_buf_ = false;
            pend_ = RESERVED_HEADER_SPACE * 2;
            gpos_ = RESERVED_HEADER_SPACE;
            ppos_ = RESERVED_HEADER_SPACE;
        }

        /// <summary>
        /// Create a new instance using an existing buffer. Since the buffer is already
        /// managed outside StreamBuffer, we don't want to free that space in destructor.
        /// </summary>
        /// <param name="buf">The buffer.</param>
        /// <param name="size">Buffer size.</param>
        StreamBuffer(const char * buf, size_t size)
            : buf_(const_cast<char*>(buf)),
            const_buf_(true),
            pend_(size),
            gpos_(0),
            ppos_(size)
        {
        }

        StreamBuffer(size_t size)
            : buf_((char*)malloc(size)),
            const_buf_(false),
            pend_(size),
            gpos_(0),
            ppos_(0)
        {
        }

        ~StreamBuffer()
        {
            if (!const_buf_)
            {
                free(buf_);
            }
        }

        void swap(StreamBuffer & rhs)
        {
            std::swap(const_buf_, rhs.const_buf_);
            std::swap(buf_, rhs.buf_);
            std::swap(pend_, rhs.pend_);
            std::swap(gpos_, rhs.gpos_);
            std::swap(ppos_, rhs.ppos_);
        }

        char * get_buf()
        {
            return buf_ + gpos_;
        }

        void set_buf(const char * buf, size_t size)
        {
            const_buf_ = true;
            buf_ = const_cast<char*>(buf);
            ppos_ = size;
            gpos_ = 0;
            pend_ = size;
        }

        void set_buf(char * buf, size_t size)
        {
            const_buf_ = false;
            buf_ = buf;
            ppos_ = size;
            gpos_ = 0;
            pend_ = size;
        }

        size_t get_size()
        {
            return ppos_ - gpos_;
        }

        template<class T>
        void write(const T & val)
        {
            ASSERT(std::is_pod<T>::value, "StreamBuffer::write(T) not implemented for %s.", typeid(T).name());
            write((char*)&val, sizeof(val));
        }

        void write(const void * buf, size_t size)
        {
            ASSERT(!const_buf_, "writing into a const buffer is not allowed.");
            if (buf_ == nullptr)
            {
                init_ostream();
            }
            size_t new_size = size + ppos_;
            if (new_size > pend_)
            {
                // reallocate buffer
                LOG("buffer is full, reallocating. pend_ = %d, new_size = %d", pend_, new_size);
                new_size = std::max(new_size, ppos_ + GROW_SIZE);
                char * new_buf = (char *)realloc(buf_, new_size);
                ASSERT(new_buf, "realloc failed");
                buf_ = new_buf;
                pend_ = new_size;
            }
            memcpy(buf_ + ppos_, buf, size);
            ppos_ += size;
        }

        template<class T>
        void read(T & val)
        {
            ASSERT(std::is_pod<T>::value, "StreamBuffer::read(T&) not implemented for %s.", typeid(T).name());
            read(&val, sizeof(val));
        }

        void read(void * buf, size_t size)
        {
            ASSERT(gpos_ + size <= ppos_,
                "reading beyond the array: required size = %d, actual size = %d", size, ppos_ - gpos_);
            memcpy(buf, buf_ + gpos_, size);
            gpos_ += size;
            if (gpos_ > GROW_SIZE && SHRINK_WITH_GET && !const_buf_)
            {
                memmove(buf_, buf_ + gpos_, ppos_ - gpos_);
                char * new_buf = (char *)realloc(buf_, pend_ - gpos_);
                ASSERT(new_buf, "realloc failed");
                buf_ = new_buf;
                pend_ -= gpos_;
                ppos_ -= gpos_;
                gpos_ = 0;
            }
        }

        template<class T>
        void write_head(const T & val)
        {
            static_assert(std::is_pod<T>::value, "StreamBuffer::write_head(T) not implemented for this type.");
            write_head((char*)&val, sizeof(val));
        }

        void write_head(const char * buf, size_t size)
        {
            ASSERT(!const_buf_, "writing into a const buffer is not allowed.");
            if (gpos_ < size)
            {
                // this should rarely happen, since we already have 64-byte reserved
                WARN("reallocating due to write_head, possible performance loss. gpos_ = %d, size = %d", gpos_, size);
                size_t new_size = std::max(size + ppos_, ppos_ + RESERVED_HEADER_SPACE);
                char * new_buf = (char *)malloc(new_size);
                ASSERT(new_buf, "realloc failed");
                // copy existing contents to the new buffer
                size_t new_gpos = new_size - (ppos_ - gpos_);
                memcpy(new_buf + new_gpos, buf_ + gpos_, ppos_ - gpos_);
                free(buf_);
                buf_ = new_buf;
                gpos_ = new_gpos;
                ppos_ = pend_ = new_size;
            }
            gpos_ -= size;
            memcpy(buf_ + gpos_, buf, size);
        }

    public:
        StreamBuffer(const StreamBuffer & rhs){};
        StreamBuffer & operator = (const StreamBuffer & rhs){ return *this; }

        char * buf_;
        bool const_buf_;// const buffers should not be written into
        size_t pend_;   // end of buffer0
        size_t gpos_;   // start of get
        size_t ppos_;   // start of put
        
        friend class TinyCommAsio;
    };

    class ResizableBuffer
    {
    public:
        ResizableBuffer()
            : buf_(nullptr),
            size_(0),
            received_bytes_(0)
        {}

        ResizableBuffer(size_t size)
            : buf_(malloc(size)),
            size_(size),
            received_bytes_(0)
        {}

        ~ResizableBuffer()
        {
            free(buf_);
        }

        void resize(size_t size)
        {
            void * newbuf = realloc(buf_, size);
            ASSERT(newbuf != nullptr, "realloc failed, original size=%lld, target size=%lld", size_, size);
            buf_ = newbuf;
            size_ = size;
        }

        size_t size()
        {
            return size_;
        }

        void * get_buf()
        {
            return buf_;
        }

        size_t get_received_bytes()
        {
            return received_bytes_;
        }

        void mark_receive_bytes(size_t size)
        {
            received_bytes_ += size;
        }

        // get buf pointer
        void * get_writable_buf()
        {
            return (char*)buf_ + received_bytes_;
        }

        // get writable size
        size_t get_writable_size()
        {
            return size_ - received_bytes_;
        }

        // take out the buf, release the ownership of the pointer
        // and malloc a new buffer
        void * renew_buf(size_t size)
        {
            void * b = buf_;
            buf_ = malloc(size);
            size_ = size;
            received_bytes_ = 0;
            return b;
        }

        // move the 
        void compact(uint64_t offset)
        {
            ASSERT(offset <= received_bytes_, 
                "compacting beyond received bytes: offset = %lld, received_bytes = %lld",
                offset, received_bytes_);
            received_bytes_ -= offset;
            memmove(buf_, (char*)buf_ + offset, received_bytes_);
        }
    private:
        ResizableBuffer(const ResizableBuffer &){};
        ResizableBuffer & operator=(const ResizableBuffer &){ return *this; }
        void * buf_;
        size_t size_;
        size_t received_bytes_;
    };

}
