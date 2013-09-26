#pragma once

#include <map>
#include <set>
#include <deque>
#include <list>
#include <vector>

namespace TinyRPC
{
//----------------------------------------------
// Marshal/UnMarshal 

template<class T>
inline std::ostream& Marshall(std::ostream &os, const T &val)
{
	os.write((char *)&val, sizeof T);
	return os;
}

template<class T>
inline std::istream& UnMarshall(std::istream &is, T &val)
{
	is.read((char *)&val, sizeof T);
	return is;
}

template<class T>
inline std::ostream& Marshall(std::ostream &os, T* val)
{
	Marshall(os, *val);
	return os;
}

template<class T>
inline std::istream& UnMarshall(std::istream &is, T* &val)
{
	val = new T;
    UnMarshall(is, *val);
	return is;
}

inline std::ostream& Marshall(std::ostream &os, const std::stringstream &val)
{
	std::string v = val.str();
	Marshall(os, (int)v.size());
	os.write((const char*)v.c_str(), v.size());
	return os;
}

inline std::istream& UnMarshall(std::istream &is, std::stringstream &val)
{
	const int buffer_size = 512;
	int len;
	UnMarshall(is, len);

	char buffer[buffer_size];
	
	for (int i = len / buffer_size; i > 0; i--)
	{
		is.read((char*)buffer, buffer_size);
		val.write(buffer, buffer_size);
	}

	is.read((char*)buffer, len % buffer_size);
	val.write(buffer, len % buffer_size);
	return is;
}

template<>
inline std::ostream& Marshall(std::ostream &os, const std::string &str)
{
	Marshall(os, (uint32_t)str.length());
    if (str.length() != 0)
    {
    	os.write((char *)str.c_str(), (std::streamsize)str.length());
    }
	return os;
}

template<>
inline std::istream& UnMarshall (std::istream &is, std::string &str)
{
	uint32_t size;
	UnMarshall(is, size);
    if (size == 0)
    {
        str = std::string("");
    }
    else
    {
		str.resize(size);
		is.read(&str[0], size);
    }
	return is;
}

template<class _First, class _Second>
inline std::ostream & Marshall (std::ostream &os,const std::pair<_First, _Second> &data)
{
	Marshall(os, data.first);
	Marshall(os, data.second);

	return os;
}

template<class _First, class _Second>
inline std::istream & UnMarshall (std::istream &is, std::pair<_First, _Second> &data)
{
	UnMarshall(is, data.first);
	UnMarshall(is, data.second);

	return is;
}

template<class _Ty>
inline std::ostream & Marshall (std::ostream &os,const std::vector<_Ty> &data)
{
	Marshall(os,(uint32_t)data.size());
	for(size_t i = 0; i < data.size(); i++)
	{
		Marshall(os, data[i]);
	}
	return os;
}

template<class _Ty>
inline std::istream & UnMarshall (std::istream &is, std::vector<_Ty> &data)
{
	uint32_t size;
	data.clear();
	UnMarshall(is, size);
	data.resize(size);
	try
	{
		for(uint32_t i = 0; i < size; i++)
		{
			UnMarshall(is, data[i]);
		}
	}
	catch(exception e)
	{

	}
	return is;
}

template<class _Kty, class _Ty, class _Pr, class _Alloc>
inline std::ostream & Marshall (std::ostream &os,const std::map<_Kty, _Ty, _Pr, _Alloc> &data)
{
	Marshall(os, (uint32_t)data.size());
	for (std::map<_Kty, _Ty, _Pr, _Alloc>::const_iterator it = data.begin(); it != data.end(); it++)
	{
		Marshall(os, it->first);
		Marshall(os, it->second);
	}
	return os;
}

template<class _Kty, class _Ty, class _Pr, class _Alloc>
inline std::istream & UnMarshall (std::istream &is, std::map<_Kty, _Ty, _Pr, _Alloc> &data)
{
	uint32_t size;

	data.clear();
	UnMarshall(is, size);
	for (uint32_t i = 0 ; i < size ; i++ )
	{
		_Kty k;
		_Ty t;
		UnMarshall(is, k);
		UnMarshall(is, t);
		data.insert(std::map<_Kty, _Ty, _Pr, _Alloc>::value_type(k,t));
	}
	return is;
}

template<class _Kty, class _Pr, class _Alloc>
inline std::ostream & Marshall (std::ostream &os,const std::set<_Kty, _Pr, _Alloc> &data)
{
	Marshall(os, (uint32_t)data.size());
	for (std::set<_Kty, _Pr, _Alloc>::const_iterator it = data.begin(); it != data.end(); it++)
	{
		Marshall(os, *it);		
	}
	return os;
}

template<class _Kty, class _Pr, class _Alloc>
inline std::istream & UnMarshall (std::istream &is, std::set<_Kty, _Pr, _Alloc> &data)
{
	uint32_t size;

	data.clear();
	UnMarshall(is, size);
	for (uint32_t i = 0 ; i < size ; i++ )
	{
		_Kty k;
		UnMarshall(is, k);
		data.insert(std::set<_Kty, _Pr, _Alloc>::value_type(k));
	}
	return is;
}

template<class _Kty>
inline std::ostream & Marshall (std::ostream &os,const std::deque<_Kty>& data)
{
	Marshall(os, (uint32_t)data.size());
	for (std::deque<_Kty>::const_iterator it = data.begin(); it != data.end(); it++)
	{
		Marshall(os, *it);		
	}
	return os;
}

template<class _Kty>
inline std::istream & UnMarshall (std::istream &is, std::deque<_Kty> &data)
{
	uint32_t size;

	data.clear();
	UnMarshall(is, size);
	for (uint32_t i = 0 ; i < size ; i++ )
	{
		_Kty k;
		UnMarshall(is, k);
		data.push_back(std::deque<_Kty>::value_type(k));
	}
	return is;
}

template<class _Kty, class _Alloc>
inline std::ostream & Marshall (std::ostream &os,const std::list<_Kty, _Alloc>& data)
{

	Marshall(os, (uint32_t)data.size());
	for (std::list<_Kty>::const_iterator it = data.begin(); it != data.end(); it++)
	{
		Marshall(os, *it);		
	}
	return os;
}

template<class _Kty, class _Alloc>
inline std::istream & UnMarshall (std::istream &is, std::list<_Kty, _Alloc> &data)
{
	uint32_t size;

	data.clear();
	UnMarshall(is, size);
	for (uint32_t i = 0 ; i < size ; i++ )
	{
		_Kty k;
		UnMarshall(is, k);
		data.push_back(std::list<_Kty, _Alloc>::value_type(k));
	}
	return is;
}

};