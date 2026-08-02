#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> IDMath_logger;
} // namespace ID

// 日志宏，方便调用 ID 的日志器
#define IDMATH_TRACE(...)      ::ID::IDMath_logger->trace(__VA_ARGS__)
#define IDMATH_DEBUG(...)      ::ID::IDMath_logger->debug(__VA_ARGS__)   
#define IDMATH_INFO(...)       ::ID::IDMath_logger->info(__VA_ARGS__)
#define IDMATH_WARN(...)       ::ID::IDMath_logger->warn(__VA_ARGS__)
#define IDMATH_ERROR(...)      ::ID::IDMath_logger->error(__VA_ARGS__)
