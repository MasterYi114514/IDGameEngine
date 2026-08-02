#include "Log/Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> ID_Window_logger = Log::create_logger("ID Window", ID::Log::Level::Trace);
} // namespace ID