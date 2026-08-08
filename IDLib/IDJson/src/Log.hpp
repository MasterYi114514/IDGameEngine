#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> IDJson_logger;
} // namespace ID

#define IDJSON_TRACE(...)      ::ID::IDJson_logger->trace(__VA_ARGS__)
#define IDJSON_DEBUG(...)      ::ID::IDJson_logger->debug(__VA_ARGS__)
#define IDJSON_INFO(...)       ::ID::IDJson_logger->info(__VA_ARGS__)
#define IDJSON_WARN(...)       ::ID::IDJson_logger->warn(__VA_ARGS__)
#define IDJSON_ERROR(...)      ::ID::IDJson_logger->error(__VA_ARGS__)