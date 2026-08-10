#include "Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> IDAudio_logger = Log::create_logger("IDAudio", ID::Log::Level::Trace);
} // namespace ID
