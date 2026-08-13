#include <Log/Log.hpp>

namespace ID
{
    std::shared_ptr<Logger>& get_ID_logger()
    {
        static std::shared_ptr<Logger> logger = Log::create_logger("ID", ID::Log::Level::Info);
        return logger;
    }
} // namespace ID