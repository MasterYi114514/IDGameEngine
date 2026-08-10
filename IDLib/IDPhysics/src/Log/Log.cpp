#include "Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> IDPhysics_logger = Log::create_logger("IDPhysics", ID::Log::Level::Trace);
} // namespace ID
