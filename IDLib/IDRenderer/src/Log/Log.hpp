#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> IDRenderer_logger;
} // namespace ID

// 日志宏，方便调用 ID 的日志器
#define IDR_TRACE(...)      ::ID::IDRenderer_logger->trace(__VA_ARGS__)
#define IDR_DEBUG(...)      ::ID::IDRenderer_logger->debug(__VA_ARGS__)   
#define IDR_INFO(...)       ::ID::IDRenderer_logger->info(__VA_ARGS__)
#define IDR_WARN(...)       ::ID::IDRenderer_logger->warn(__VA_ARGS__)
#define IDR_ERROR(...)      ::ID::IDRenderer_logger->error(__VA_ARGS__)
