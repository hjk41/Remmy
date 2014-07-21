#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <assert.h>

namespace TinyRPC
{

    enum LogLevel
    {
        LOG_INFO = 0,
        LOG_WARNING = 1,
        LOG_ERROR = 2
    };

    #define LOG_LEVEL 0

    inline void OctopusLog(LogLevel level,
        const char * component,
        const char * filename,
        int lineNum,
        const char * format, ...)
    {
        if (LOG_LEVEL > level)
        {
            return;
        }
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
            fprintf(stdout, "OctopusLog[%s:%d]: ", filename, lineNum);
            break;
        case LOG_WARNING:
            fprintf(stdout, "OctopusWarn[%s:%d]: ", filename, lineNum);
            break;
        case LOG_ERROR:
            fprintf(stdout, "OctopusErr[%s:%d]: ", filename, lineNum);
            break;
        }
#endif
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
        fprintf(stdout, "\n\n");
        fflush(stdout);
        if (level == LogLevel::LOG_ERROR)
        {
            std::abort();
        }
    }

#undef LOGGING_COMPONENT

#define LOGGING_COMPONENT "common"

#define LOG(format, ...) \
    do{ OctopusLog(LOG_INFO, LOGGING_COMPONENT, __FILE__, __LINE__, (format), ##__VA_ARGS__); } while (0)

#define WARN(format, ...) \
    do{ OctopusLog(LOG_WARNING, LOGGING_COMPONENT, __FILE__, __LINE__, (format), ##__VA_ARGS__); } while (0)

#define ABORT(format, ...) \
    do{ OctopusLog(LOG_ERROR, LOGGING_COMPONENT, __FILE__, __LINE__, (format), ##__VA_ARGS__); } while (0)

#define ASSERT(pred, format, ...) \
    do{ if (!(pred)) ABORT((format), ##__VA_ARGS__); } while (0)


};