#include "Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> IDAsset_logger  = Log::create_logger("IDAsset", ID::Log::Level::Trace);
} // namespace ID