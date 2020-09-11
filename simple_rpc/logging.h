#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <assert.h>
#include <mutex>

namespace simple_rpc {
#define SIMPLE_LOG_INFO 0
#define SIMPLE_LOG_WARNING 1
#define SIMPLE_LOG_ERROR 2
#define SIMPLE_LOG_ASSERT 3

#define SIMPLE_LOG_LEVEL 1

    inline std::mutex& __Log_Lock__() {
        static std::mutex log_lock;
        return log_lock;
    }

    inline void SimpleLogger(int level,
                             const char *component,
                             const char *filename,
                             int lineNum,
                             int enabled_level,
                             const char *format, ...) {
        if (level < enabled_level) {
            return;
        }
        std::lock_guard<std::mutex> l(__Log_Lock__());
        va_list args;
#ifdef PRINT_COMPONENT
        switch (level) {
        case SIMPLE_LOG_INFO:
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
        case SIMPLE_LOG_INFO:
            fprintf(stdout, "SIMPLE_LOG[%s:%d]: ", filename, lineNum);
            break;
        case SIMPLE_LOG_WARNING:
            fprintf(stdout, "SIMPLE_WARN[%s:%d]: ", filename, lineNum);
            break;
        case SIMPLE_LOG_ERROR:
            fprintf(stdout, "Err[%s:%d]: ", filename, lineNum);
            break;
        }
#endif
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
        fprintf(stdout, "\n");
        fflush(stdout);
        if (level == SIMPLE_LOG_ERROR) {
            std::exit(-1);
        }
    }

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "common"

#if (SIMPLE_LOG_INFO >= SIMPLE_LOG_LEVEL)
#define SIMPLE_LOG(format, ...) \
    do{ SimpleLogger(SIMPLE_LOG_INFO, LOGGING_COMPONENT, __FILE__, __LINE__, SIMPLE_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define SIMPLE_LOG(...)
#endif

#if (SIMPLE_LOG_WARNING >= SIMPLE_LOG_LEVEL)
#define SIMPLE_WARN(format, ...) \
    do{ SimpleLogger(SIMPLE_LOG_WARNING, LOGGING_COMPONENT, __FILE__, __LINE__, SIMPLE_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define SIMPLE_WARN(...)
#endif

#if (SIMPLE_LOG_ERROR >= SIMPLE_LOG_LEVEL)
#define SIMPLE_ABORT(format, ...) \
    do{ SimpleLogger(SIMPLE_LOG_ERROR, (LOGGING_COMPONENT), (__FILE__), (__LINE__), SIMPLE_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define SIMPLE_ABORT(...) SIMPLE_ABORT();
#endif

#if (SIMPLE_LOG_ASSERT >= SIMPLE_LOG_LEVEL)
#define SIMPLE_ASSERT(pred, format, ...) \
    do{ if (!(pred)) SIMPLE_ABORT((format), ##__VA_ARGS__); } while (0)
#else
#define SIMPLE_ASSERT(...)
#endif

#define SIMPLE_SUICIDE(format, ...) \
    do{ SimpleLogger(SIMPLE_LOG_WARNING, (LOGGING_COMPONENT), (__FILE__), (__LINE__), SIMPLE_LOG_LEVEL, (format), ##__VA_ARGS__); exit(0); } while (0)
};
