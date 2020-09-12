#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <assert.h>
#include <mutex>

namespace remmy {
#define REMMY_LOG_INFO 0
#define REMMY_LOG_WARNING 1
#define REMMY_LOG_ERROR 2
#define REMMY_LOG_ASSERT 3

#define REMMY_LOG_LEVEL 1

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
        case REMMY_LOG_INFO:
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
        case REMMY_LOG_INFO:
            fprintf(stdout, "REMMY_LOG[%s:%d]: ", filename, lineNum);
            break;
        case REMMY_LOG_WARNING:
            fprintf(stdout, "REMMY_WARN[%s:%d]: ", filename, lineNum);
            break;
        case REMMY_LOG_ERROR:
            fprintf(stdout, "Err[%s:%d]: ", filename, lineNum);
            break;
        }
#endif
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
        fprintf(stdout, "\n");
        fflush(stdout);
        if (level == REMMY_LOG_ERROR) {
            std::exit(-1);
        }
    }

#undef LOGGING_COMPONENT
#define LOGGING_COMPONENT "common"

#if (REMMY_LOG_INFO >= REMMY_LOG_LEVEL)
#define REMMY_LOG(format, ...) \
    do{ SimpleLogger(REMMY_LOG_INFO, LOGGING_COMPONENT, __FILE__, __LINE__, REMMY_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define REMMY_LOG(...)
#endif

#if (REMMY_LOG_WARNING >= REMMY_LOG_LEVEL)
#define REMMY_WARN(format, ...) \
    do{ SimpleLogger(REMMY_LOG_WARNING, LOGGING_COMPONENT, __FILE__, __LINE__, REMMY_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define REMMY_WARN(...)
#endif

#if (REMMY_LOG_ERROR >= REMMY_LOG_LEVEL)
#define REMMY_ABORT(format, ...) \
    do{ SimpleLogger(REMMY_LOG_ERROR, (LOGGING_COMPONENT), (__FILE__), (__LINE__), REMMY_LOG_LEVEL, (format), ##__VA_ARGS__); } while (0)
#else
#define REMMY_ABORT(...) REMMY_ABORT();
#endif

#if (REMMY_LOG_ASSERT >= REMMY_LOG_LEVEL)
#define REMMY_ASSERT(pred, format, ...) \
    do{ if (!(pred)) REMMY_ABORT((format), ##__VA_ARGS__); } while (0)
#else
#define REMMY_ASSERT(...)
#endif

#define SIMPLE_SUICIDE(format, ...) \
    do{ SimpleLogger(REMMY_LOG_WARNING, (LOGGING_COMPONENT), (__FILE__), (__LINE__), REMMY_LOG_LEVEL, (format), ##__VA_ARGS__); exit(0); } while (0)
};
