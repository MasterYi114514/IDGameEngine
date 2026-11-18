#include "project/Log/Log.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "project/ProjectLifeCycle.hpp"

/**
 *  项目日志器的唯一定义与初始化实现。
 *
 *  初始化步骤固定（skills/log-style 第 4.2 节）：`create_logger("IDProject")` → 检查句柄有效 →
 *  `get_logger` 取指针并检查非空 → 赋给模块日志器 → 按环境变量设置 console 目标阈值 →
 *  打一条 INFO 表示链路已通。两个失败分支与阈值设置失败都直接返回 false，且**不调用任何日志宏**
 *  （此时宏体内的空指针没有兜底，调用即未定义行为）。
 *
 *  输出目的地：只有 console 目标（stdout）；本单元不打开、不创建、不命名任何日志文件。
 */

namespace ID::Project
{
    ::ID::Log::Logger* IDProject_logger = nullptr;

    namespace
    {
        // 非法级别取值只向 stderr 提示一次（每进程一次）
        bool g_is_level_warned = false;

        // 级别取值：trace/debug/info/warn/error/fatal；未设置或非法一律按 info
        ::ID::Log::Level parse_level(std::string_view text, bool& is_invalid) noexcept
        {
            is_invalid = false;

            if (text.empty())
            {
                return ::ID::Log::Level::Info;
            }
            if (text == "trace")
            {
                return ::ID::Log::Level::Trace;
            }
            if (text == "debug")
            {
                return ::ID::Log::Level::Debug;
            }
            if (text == "info")
            {
                return ::ID::Log::Level::Info;
            }
            if (text == "warn")
            {
                return ::ID::Log::Level::Warn;
            }
            if (text == "error")
            {
                return ::ID::Log::Level::Error;
            }
            if (text == "fatal")
            {
                return ::ID::Log::Level::Fatal;
            }

            is_invalid = true;
            return ::ID::Log::Level::Info;
        }

        // 非法级别的一次性提示：不经诊断通道，直接写 stderr（诊断通道只放 ProjectDiagnostic）
        void report_invalid_level(const char* text) noexcept
        {
            if (g_is_level_warned)
            {
                return;
            }
            g_is_level_warned = true;

            std::fputs("ID_PROJECT_LOG_LEVEL 取值非法，按 info 处理：", stderr);
            std::fputs(text == nullptr ? "" : text, stderr);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }
    } // namespace
} // namespace ID::Project

namespace ID::Project::LifeCycle::OnInit
{
    bool IDProject_logger_init() noexcept
    {
        const ::ID::Log::LoggerHandle logger_handle = ::ID::Log::create_logger("IDProject");
        if (!logger_handle.is_valid())
        {
            return false;
        }

        IDProject_logger = ::ID::Log::get_logger(logger_handle);
        if (IDProject_logger == nullptr)
        {
            return false;
        }

        const char* const            level_text = std::getenv("ID_PROJECT_LOG_LEVEL");
        bool                         is_invalid = false;
        const ::ID::Log::Level       level =
            parse_level(level_text == nullptr ? std::string_view{} : std::string_view(level_text), is_invalid);
        if (is_invalid)
        {
            report_invalid_level(level_text);
        }

        // 阈值设置失败时不带着未生效的级别继续运行
        if (!IDProject_logger->set_sink_level(::ID::Log::SinkType::Console, level))
        {
            return false;
        }

        IDPROJECT_LOG_INFO("IDProject_logger_init: Logger 创建成功");
        return true;
    }
} // namespace ID::Project::LifeCycle::OnInit

namespace ID::Project::LifeCycle::OnShutdown
{
    void IDProject_logger_shutdown() noexcept
    {
        // 先置空再 shutdown：返回之后任何日志宏调用都不再指向已销毁的日志器
        IDProject_logger = nullptr;
        ::ID::Log::shutdown();
    }
} // namespace ID::Project::LifeCycle::OnShutdown
