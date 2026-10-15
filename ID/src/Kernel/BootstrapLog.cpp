#include "Kernel/BootstrapLog.hpp"
#include <cstdio>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

namespace ID::Log
{
    // constinit 确保编译期零开销初始化
    constinit BootstrapLog BootstrapLog::s_instance;

    namespace
    {
        constexpr const char* level_to_string(Level level) noexcept
        {
            switch (level)
            {
                case Level::Trace: return "TRACE";
                case Level::Debug: return "DEBUG";
                case Level::Info:  return "INFO ";
                case Level::Warn:  return "WARN ";
                case Level::Error: return "ERROR";
                case Level::Fatal: return "FATAL";
                default:           return "UNK  ";
            }
        }
    } // namespace

    void default_bootstrap_sink(Level level, std::string_view category, std::string_view message) noexcept
    {
        char buffer[512];
        int len = std::snprintf(
            buffer, sizeof(buffer), "[%s][%.*s] %.*s\n",
            level_to_string(level),
            static_cast<int>(category.size()), category.data(),
            static_cast<int>(message.size()), message.data()
        );

        if (len > 0)
        {
            size_t write_len = static_cast<size_t>(len) < sizeof(buffer) ? static_cast<size_t>(len) : sizeof(buffer) - 1;
            std::fwrite(buffer, 1, write_len, stderr);

            #if defined(_WIN32)
                OutputDebugStringA(buffer);
            #endif
        }
    }

    ID_NOINLINE void BootstrapLog::report(Level level, std::string_view category, std::string_view message) const noexcept
    {
        SinkFn sink = m_active_sink.load(std::memory_order_relaxed);
        sink(level, category, message);
    }

    ID_NOINLINE void BootstrapLog::report_assert(std::string_view expr, std::string_view message, std::string_view file, uint32_t line) const noexcept
    {
        char buffer[512];
        std::snprintf(
            buffer, sizeof(buffer), "ASSERT FAILED: (%.*s) | %.*s [%.*s:%u]",
            static_cast<int>(expr.size()), expr.data(),
            static_cast<int>(message.size()), message.data(),
            static_cast<int>(file.size()), file.data(),
            line
        );
        report(Level::Fatal, "ID.Assert", buffer);
    }
} // namespace ID::Log
