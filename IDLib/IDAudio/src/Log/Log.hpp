#pragma once

#include "IDLogger.hpp"

namespace ID
{
    extern std::shared_ptr<Logger> IDAudio_logger;
} // namespace ID

#define IDAUDIO_TRACE(...)    ::ID::IDAudio_logger->trace(__VA_ARGS__)
#define IDAUDIO_DEBUG(...)    ::ID::IDAudio_logger->debug(__VA_ARGS__)
#define IDAUDIO_INFO(...)     ::ID::IDAudio_logger->info(__VA_ARGS__)
#define IDAUDIO_WARN(...)     ::ID::IDAudio_logger->warn(__VA_ARGS__)
#define IDAUDIO_ERROR(...)    ::ID::IDAudio_logger->error(__VA_ARGS__)
