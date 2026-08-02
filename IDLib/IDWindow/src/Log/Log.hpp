#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> ID_Window_logger;
} // namespace ID

// 日志宏，方便调用 ID 的日志器
#define ID_WINDOW_TRACE(...)      ::ID::ID_Window_logger->trace(__VA_ARGS__)
#define ID_WINDOW_DEBUG(...)      ::ID::ID_Window_logger->debug(__VA_ARGS__)   
#define ID_WINDOW_INFO(...)       ::ID::ID_Window_logger->info(__VA_ARGS__)
#define ID_WINDOW_WARN(...)       ::ID::ID_Window_logger->warn(__VA_ARGS__)
#define ID_WINDOW_ERROR(...)      ::ID::ID_Window_logger->error(__VA_ARGS__)