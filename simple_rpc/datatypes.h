#pragma once

namespace simple_rpc
{
    enum class TinyErrorCode
    {
        SUCCESS = 0,
        FAIL_SEND = 1,
        TIMEOUT = 2,
        SERVER_FAIL = 3,
        KILLING_THREADS = 4
    };

}