#pragma once

namespace simple_rpc
{
    enum class ErrorCode
    {
        SUCCESS = 0,
        FAIL_SEND = 1,
        TIMEOUT = 2,
        SERVER_FAIL = 3,
        KILLING_THREADS = 4
    };

}