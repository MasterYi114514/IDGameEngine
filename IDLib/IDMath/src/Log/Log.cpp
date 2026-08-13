#include "Log/Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> IDMath_logger = Log::create_logger("IDMath", ID::Log::Level::Info);
} // namespace ID