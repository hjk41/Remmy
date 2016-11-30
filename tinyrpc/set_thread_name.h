#pragma once
#include <cstdint>
#include <string>

namespace tinyrpc {
    #ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    const DWORD MS_VC_EXCEPTION = 0x406D1388;

    #pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO {
        DWORD dwType; // Must be 0x1000.
        LPCSTR szName; // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags; // Reserved for future use, must be zero.
    } THREADNAME_INFO;
    #pragma pack(pop)

    inline static void SetThreadName(uint32_t dwThreadID, const char* threadName) {
        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = threadName;
        info.dwThreadID = dwThreadID;
        info.dwFlags = 0;

        __try {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    inline void SetThreadName(const char* threadName) {
        SetThreadName(GetCurrentThreadId(), threadName);
    }

    inline void SetThreadName(const char * threadName, int threadId) {
        std::string str = std::string(threadName) + std::to_string(threadId);
        SetThreadName(str.c_str());
    }

    //void SetThreadName(std::thread* thread, const char* threadName)
    //{
    //    DWORD threadId = ::GetThreadId(static_cast<HANDLE>(thread->native_handle()));
    //    SetThreadName(threadId, threadName);
    //}
    #else
    inline void SetThreadName(std::thread* thread, const char* threadName) {
        auto handle = thread->native_handle();
        pthread_setname_np(handle, threadName);
    }


    #include <sys/prctl.h>
    inline void SetThreadName(const char* threadName) {
        prctl(PR_SET_NAME, threadName, 0, 0, 0);
    }

    inline void SetThreadName(const char * threadName, int threadId) {
        std::string str = "RPC worker " + std::to_string(threadId);
        SetThreadName(str.c_str());
    }
    #endif
}