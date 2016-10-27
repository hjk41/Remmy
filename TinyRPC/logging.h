#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <assert.h>
#include <mutex>

namespace TinyRPC
{
    void SetThreadName(const char *);
    void SetThreadName(const char *, int);

#define LOG_INFO 0
#define LOG_WARNING 1
#define LOG_ERROR 2
#define LOG_ASSERT 3

#define LOG_LEVEL 0


    extern std::mutex * __logLock;
    inline void OctopusLog(int level,
        const char * component,
        const char * filename,
        int lineNum,
        int enabled_level,
        const char * format, ...)
    {
        if (level < enabled_level)
        {
            return;
        }
        std::lock_guard<std::mutex> l(*__logLock);
        va_list args;
#ifdef PRINT_COMPONENT
        switch (level)
        {
        case LOG_INFO:
            fprintf(stdout, "OctopusLog[%s][%s:%d]: ", component, filename, lineNum);
            break;
        case LOG_WARNING:
            fprintf(stdout, "OctopusWarn[%s][%s:%d]: ", component, filename, lineNum);
            break;
        case LOG_ERROR:
            fprintf(stdout, "OctopusErr[%s][%s:%d]: ", component, filename, lineNum);
            break;
        }
#else
        switch (level)
        {
        case LOG_INFO:
            fprintf(stdout, "Log[%s:%d]: ", filename, lineNum);
            break;
        case LOG_WARNING:
            fprintf(stdout, "Warn[%s:%d]: ", filename, lineNum);
            break;
        case LOG_ERROR:
            fprintf(stdout, "Err[%s:%d]: ", filename, lineNum);
            break;
        }
#endif
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
        fprintf(stdout, "\n");
        fflush(stdout);
        if (level == LOG_ERROR)
        {
            std::exit(-1);
        }
    }

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "common"

#if (LOG_INFO >= LOG_LEVEL)
#define LOG(format, ...) \
    do{ OctopusLog(LOG_INFO, LOGGING_COMPONENT, __FILE__, __LINE__, LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define LOG(...) 
#endif

#if (LOG_WARNING >= LOG_LEVEL)
#define WARN(format, ...) \
    do{ OctopusLog(LOG_WARNING, LOGGING_COMPONENT, __FILE__, __LINE__, LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define WARN(...)
#endif

#if (LOG_ERROR >= LOG_LEVEL)
#define ABORT(format, ...) \
    do{ OctopusLog(LOG_ERROR, (LOGGING_COMPONENT), (__FILE__), (__LINE__), LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define ABORT(...) abort();
#endif

#if (LOG_ASSERT >= LOG_LEVEL)
#define ASSERT(pred, format, ...) \
    do{ if (!(pred)) ABORT((format), ##__VA_ARGS__); } while (0)
#else
#define ASSERT(...) 
#endif

#define SUICIDE(format, ...) \
    do{ OctopusLog(LOG_WARNING, (LOGGING_COMPONENT), (__FILE__), (__LINE__), LOG_LEVEL, (format), ##__VA_ARGS__); exit(0); } while (0)


};
