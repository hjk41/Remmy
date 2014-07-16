#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <assert.h>

namespace TinyRPC
{

enum LogPriority
{
	LOG_NORMAL,
	LOG_NOTICE,
	LOG_WARNING,
	LOG_ERROR
};

inline void TinyLog(LogPriority code, const char* format, ... ) {
    va_list args;
    fprintf( stdout, "TinyLog: " );
    va_start( args, format );
    vfprintf( stdout, format, args );
    va_end( args );
    fprintf( stdout, "\n" );
	fflush(stdout);
	if (code == LOG_ERROR)
		assert(0);
}

inline void TinyAssert(bool predicate, const char* format, ... ) {
#ifdef _DEBUG
	if (!predicate)
	{
		va_list args;
		fprintf( stderr, "TinyLog: " );
		va_start( args, format );
		vfprintf( stderr, format, args );
		va_end( args );
		fprintf( stderr, "\n" );
		assert(0);
		fflush(stderr);
	}
#endif
}


};