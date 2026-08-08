#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> IDAsset_logger;
} // namespace ID

#define IDASSET_TRACE(...)      ::ID::IDAsset_logger->trace(__VA_ARGS__)
#define IDASSET_DEBUG(...)      ::ID::IDAsset_logger->debug(__VA_ARGS__)
#define IDASSET_INFO(...)       ::ID::IDAsset_logger->info(__VA_ARGS__)
#define IDASSET_WARN(...)       ::ID::IDAsset_logger->warn(__VA_ARGS__)
#define IDASSET_ERROR(...)      ::ID::IDAsset_logger->error(__VA_ARGS__)