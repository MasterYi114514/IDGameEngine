#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> IDPhysics_logger;
} // namespace ID

#define IDPHYSICS_TRACE(...)    ::ID::IDPhysics_logger->trace(__VA_ARGS__)
#define IDPHYSICS_DEBUG(...)    ::ID::IDPhysics_logger->debug(__VA_ARGS__)
#define IDPHYSICS_INFO(...)     ::ID::IDPhysics_logger->info(__VA_ARGS__)
#define IDPHYSICS_WARN(...)     ::ID::IDPhysics_logger->warn(__VA_ARGS__)
#define IDPHYSICS_ERROR(...)    ::ID::IDPhysics_logger->error(__VA_ARGS__)
