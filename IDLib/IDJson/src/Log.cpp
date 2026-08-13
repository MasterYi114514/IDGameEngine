#include "Log.hpp"

namespace ID
{
    std::shared_ptr<Logger> IDJson_logger = Log::create_logger("IDJson", ID::Log::Level::Info);
}