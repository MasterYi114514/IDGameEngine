#include "Log/Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> IDRenderer_logger = Log::create_logger("IDRenderer", ID::Log::Level::Trace);
} // namespace ID