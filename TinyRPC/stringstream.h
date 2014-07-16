#pragma once
#pragma warning (disable : 4250)
#pragma warning (disable : 4018)
#include <sstream>
#include <assert.h>
#include <stdint.h>
#include "serialize.h"

namespace TinyRPC
{
	
#if 1
	
class TinyStringStreamBuf : public std::streambuf
{
	static const int MIN_SIZE = 32;
	static const int THRINK_THRESHOLD = 32*1024;
public:
	TinyStringStreamBuf() : _buf(NULL), _ppos(0), _gpos(0), _buf_size(0){};
	~TinyStringStreamBuf() { free(_buf); }
	virtual std::streamsize xsputn(const char * b,	std::streamsize n)
	{
		if (_buf_size - _ppos < n)
		{
			size_t least_size = _ppos + n;
			size_t new_size = (_buf_size*2) > MIN_SIZE ? (_buf_size*2) : MIN_SIZE;
			_buf_size = new_size > least_size ? new_size : least_size;
			_buf = (char*)realloc(_buf, _buf_size);
			assert(_buf);
		}
		memcpy(_buf + _ppos, b, n);
		_ppos += n;
		return n;
	}
	virtual std::streamsize xsgetn(char * b,	std::streamsize n)
	{
		if (_gpos + n > _ppos)
		{
			return 0;
		}
		memcpy(b, _buf+_gpos, n);
		_gpos += n;
		// shrink if too much wasted space
		if (_gpos >= THRINK_THRESHOLD && _gpos >= _buf_size/2)
		{
			memmove(_buf, _buf + _gpos, _ppos - _gpos);
			_ppos -= _gpos;
			_buf_size -= _gpos;
			_gpos = 0;
			if (_buf_size != 0)
			{
				_buf = (char*)realloc(_buf, _buf_size);
				assert(_buf);
			}
			else
			{
				free(_buf);
				_buf = NULL;
			}
		}
		return n;
	}

	inline size_t gsize() const
	{
		return _ppos - _gpos;
	}

	inline size_t psize() const
	{
		return _ppos;
	}

	inline char * get_buf() const
	{
		return _buf + _gpos;
	}

	inline void set_buf(char * p, size_t s, bool is_write = false)
	{		
		if (p != _buf)
			free(_buf);

		_buf = p;
		_ppos = is_write ? 0 : s;
		_gpos = 0;
		_buf_size = s;
	}
private:
	char * _buf;
	size_t _ppos;
	size_t _gpos;
	size_t _buf_size;
};

class TinyStringStream : public std::iostream
{
	friend std::ostream & Marshall(std::ostream & os, const TinyStringStream & ss);
	friend std::istream & UnMarshall(std::istream & is, TinyStringStream & ss);
private:
	TinyStringStream(const TinyStringStream &);
	TinyStringStream operator = (const TinyStringStream &);
	template<class T>
	TinyStringStream & operator<<(const T &);
	template<class T>
	TinyStringStream & operator>>(T &);
public:
	TinyStringStream() : std::iostream(&_sb){}
	~TinyStringStream(){}
	inline size_t gsize() const
	{
		return _sb.gsize();
	}
	inline size_t psize() const
	{
		return _sb.psize();
	}
	inline char * get_buf() const
	{
		return _sb.get_buf();
	}
	inline void set_buf(char * b, size_t n, bool is_write = false)
	{
		return _sb.set_buf(b, n, is_write);
	}
private:
	TinyStringStreamBuf _sb;
};

inline std::ostream & Marshall(std::ostream & os, const TinyStringStream & ss)
{
	size_t size = ss.gsize();
	Marshall(os, size);
	os.write(ss.get_buf(), size);
	return os;
}

inline std::istream & UnMarshall(std::istream & is, TinyStringStream & ss)
{
	size_t size;
	UnMarshall(is, size);
	char * buf = new char[size];
	is.read(buf, size);
	ss.set_buf(buf, size);
	return is;
}

#else
typedef std::stringstream TinyStringStream;
#endif

};