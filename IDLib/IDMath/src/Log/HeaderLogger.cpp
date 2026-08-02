#include "Log/Log.hpp"
#include "Core/HeaderLogger.hpp"

namespace ID::HeaderLogger
{
    void write(const std::string& message)
    {
        if (::ID::IDMath_logger)
            ::ID::IDMath_logger->write(Log::Level::Error, message);
    }
}