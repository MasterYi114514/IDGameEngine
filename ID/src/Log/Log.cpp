#include <Log/Log.hpp>

namespace ID
{
    std::shared_ptr<Logger> ID_API ID_Logger = Log::create_logger("ID", ID::Log::Level::Trace);
} // namespace ID