#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <assert.h>
#include <mutex>

namespace tinyrpc {
#define TINY_LOG_INFO 0
#define TINY_LOG_WARNING 1
#define TINY_LOG_ERROR 2
#define TINY_LOG_ASSERT 3

#define TINY_LOG_LEVEL 1

    inline std::mutex& __Log_Lock__() {
        static std::mutex log_lock;
        return log_lock;
    }

    inline void OctopusLog(int level,
        const char * component,
        const char * filename,
        int lineNum,
        int enabled_level,
        const char * format, ...) {
        if (level < enabled_level) {
            return;
        }
        std::lock_guard<std::mutex> l(__Log_Lock__());
        va_list args;
#ifdef PRINT_COMPONENT
        switch (level) {
        case TINY_LOG_INFO:
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
        switch (level) {
        case TINY_LOG_INFO:
            fprintf(stdout, "TINY_LOG[%s:%d]: ", filename, lineNum);
            break;
        case TINY_LOG_WARNING:
            fprintf(stdout, "TINY_WARN[%s:%d]: ", filename, lineNum);
            break;
        case TINY_LOG_ERROR:
            fprintf(stdout, "Err[%s:%d]: ", filename, lineNum);
            break;
        }
#endif
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
        fprintf(stdout, "\n");
        fflush(stdout);
        if (level == TINY_LOG_ERROR) {
            std::exit(-1);
        }
    }

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "common"

#if (TINY_LOG_INFO >= TINY_LOG_LEVEL)
#define TINY_LOG(format, ...) \
    do{ OctopusLog(TINY_LOG_INFO, LOGGING_COMPONENT, __FILE__, __LINE__, TINY_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define TINY_LOG(...) 
#endif

#if (TINY_LOG_WARNING >= TINY_LOG_LEVEL)
#define TINY_WARN(format, ...) \
    do{ OctopusLog(TINY_LOG_WARNING, LOGGING_COMPONENT, __FILE__, __LINE__, TINY_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define TINY_WARN(...)
#endif

#if (TINY_LOG_ERROR >= TINY_LOG_LEVEL)
#define TINY_ABORT(format, ...) \
    do{ OctopusLog(TINY_LOG_ERROR, (LOGGING_COMPONENT), (__FILE__), (__LINE__), TINY_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define TINY_ABORT(...) TINY_ABORT();
#endif

#if (TINY_LOG_ASSERT >= TINY_LOG_LEVEL)
#define TINY_ASSERT(pred, format, ...) \
    do{ if (!(pred)) TINY_ABORT((format), ##__VA_ARGS__); } while (0)
#else
#define TINY_ASSERT(...) 
#endif

#define TINY_SUICIDE(format, ...) \
    do{ OctopusLog(TINY_LOG_WARNING, (LOGGING_COMPONENT), (__FILE__), (__LINE__), TINY_LOG_LEVEL, (format), ##__VA_ARGS__); exit(0); } while (0)
};
