#pragma once

#include <cstdint>

namespace ID::Log
{
    enum class Level : uint8_t
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,

        Fatal      // 约定 Fatal 级别的日志直接强制终止程序
    };
} // namespace ID::Log