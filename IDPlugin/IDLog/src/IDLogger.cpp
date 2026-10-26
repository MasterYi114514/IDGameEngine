#include "IDLogger.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "IDLogDetail.hpp"

namespace ID::Log
{
    Logger::Logger(std::shared_ptr<spdlog::logger> backend)
        : m_backend(std::move(backend))
    {
    }

    ::ID::Common::Handle<std::uint32_t, Logger> Logger::make_handle(std::uint32_t id) noexcept
    {
        return ::ID::Common::Handle<std::uint32_t, Logger>(id);
    }

    bool Logger::is_sink_attached(const std::shared_ptr<spdlog::sinks::sink>& sink) const noexcept
    {
        if (m_backend == nullptr || sink == nullptr)
        {
            return false;
        }

        const std::vector<spdlog::sink_ptr>& sinks = m_backend->sinks();
        return std::find(sinks.begin(), sinks.end(), sink) != sinks.end();
    }

    void Logger::detach_sink(const std::shared_ptr<spdlog::sinks::sink>& sink) noexcept
    {
        if (m_backend == nullptr || sink == nullptr) [[unlikely]]
        {
            return;
        }

        try
        {
            sink->flush();
        }
        catch (...)
        {
            // 摘除目标前的冲刷失败不影响摘除本身
        }

        std::vector<spdlog::sink_ptr>& sinks = m_backend->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());

        sync_backend_level();
    }

    void Logger::sync_backend_level() noexcept
    {
        if (m_backend == nullptr) [[unlikely]]
        {
            return;
        }

        // 后端阈值取“已启用目标阈值的最小值”：低于该值的消息一定会被所有目标丢弃，
        // 因此在格式化之前就被挡掉（sink 级过滤发生在格式化之后）。单个目标的可见输出不变。
        spdlog::level::level_enum lowest = spdlog::level::off;

        if (is_sink_attached(m_console_sink))
        {
            lowest = std::min(lowest, Detail::to_spdlog_level(m_console_level));
        }

        if (is_sink_attached(m_file_sink))
        {
            lowest = std::min(lowest, Detail::to_spdlog_level(m_file_level));
        }

        m_backend->set_level(lowest);
    }

    bool Logger::attach_console_sink()
    {
        if (m_backend == nullptr) [[unlikely]]
        {
            return false;
        }

        try
        {
            if (m_console_sink == nullptr)
            {
                std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink =
                    std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_pattern(std::string(Detail::kPattern));
                console_sink->set_level(Detail::to_spdlog_level(m_console_level));
                m_console_sink = std::move(console_sink);
            }

            if (!is_sink_attached(m_console_sink))
            {
                m_backend->sinks().push_back(m_console_sink);
                sync_backend_level();
            }

            return true;
        }
        catch (...)
        {
            // 创建或接入失败时保持终端停用；异常不跨 DLL 边界
            return false;
        }
    }

    bool Logger::attach_file_sink()
    {
        // 没有成功创建过的文件目标（未设置路径或创建失败）：不能启用，也不改变现有配置
        if (m_backend == nullptr || m_file_sink == nullptr) [[unlikely]]
        {
            return false;
        }

        try
        {
            if (!is_sink_attached(m_file_sink))
            {
                m_backend->sinks().push_back(m_file_sink);   // 可抛出：此时未改动其它状态
                sync_backend_level();
            }

            return true;
        }
        catch (...)
        {
            // 接入失败时保持文件目标停用；异常不跨 DLL 边界
            return false;
        }
    }

    bool Logger::enable_sink(SinkType type)
    {
        switch (type)
        {
            case SinkType::Console: return attach_console_sink();
            case SinkType::File:    return attach_file_sink();
        }

        return false;   // 非法枚举值：不静默当成某个目标
    }

    bool Logger::disable_sink(SinkType type)
    {
        if (m_backend == nullptr) [[unlikely]]
        {
            return false;   // 条目已销毁：契约上句柄已失效
        }

        switch (type)
        {
            case SinkType::Console:
            {
                detach_sink(m_console_sink);
                return true;
            }

            case SinkType::File:
            {
                detach_sink(m_file_sink);
                return true;
            }
        }

        return false;   // 非法枚举值
    }

    bool Logger::set_sink_level(SinkType type, Level level)
    {
        if (m_backend == nullptr) [[unlikely]]
        {
            return false;   // 条目已销毁：契约上句柄已失效
        }

        // 非法 Level（强制转换后超出 Fatal）与非法 SinkType 同策略：拒绝并返回 false，不改动任何阈值。
        // 若放行，越界值会经 to_spdlog_level 落到默认分支被当成 Trace（即全量放行），属于静默降级。
        if (!Detail::is_valid_level(level)) [[unlikely]]
        {
            return false;
        }

        switch (type)
        {
            case SinkType::Console:
            {
                apply_console_level(level);
                return true;
            }

            case SinkType::File:
            {
                apply_file_level(level);
                return true;
            }
        }

        return false;   // 非法枚举值
    }

    bool Logger::set_sink_path(SinkType type, const char* path)
    {
        if (type != SinkType::File) [[unlikely]]
        {
            return false;   // 只有文件目标有路径；终端目标不接受路径
        }

        if (m_backend == nullptr || path == nullptr || *path == '\0') [[unlikely]]
        {
            return false;
        }

        // 这是导出路径：路径构造、比较、新目标创建与接入全部在 try 内，任何异常（含 bad_alloc）
        // 都不跨越 DLL 边界。所有可能抛出的步骤都排在改动状态之前，失败时旧配置原样可用。
        try
        {
            std::string requested_path = path;

            // 同一路径：幂等（已启用则直接返回，被停用则重新接入）
            if (m_file_sink != nullptr && m_file_path == requested_path)
            {
                if (!is_sink_attached(m_file_sink))
                {
                    m_backend->sinks().push_back(m_file_sink);   // 可抛出：此时未改动其它状态
                    sync_backend_level();
                }

                return true;
            }

            // 追加写入，不截断已有内容
            std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(requested_path, false);
            file_sink->set_pattern(std::string(Detail::kPattern));
            file_sink->set_level(Detail::to_spdlog_level(m_file_level));

            // 先挂载新目标：挂载失败时旧目标仍在使用，配置不变
            m_backend->sinks().push_back(file_sink);

            // 以下四步均不分配、不抛出：摘除旧目标 → 换上已挂载的新目标 → 记录新路径 → 同步后端阈值
            detach_sink(m_file_sink);
            m_file_sink = std::move(file_sink);
            m_file_path.swap(requested_path);
            sync_backend_level();
            return true;
        }
        catch (...)
        {
            // 文件打不开或内存不足：以 false 报告，异常不跨 DLL 边界；
            // 失败发生在新目标挂载之前，旧目标与其内容保持可用
            return false;
        }
    }

    void Logger::apply_console_level(Level level) noexcept
    {
        m_console_level = level;

        if (m_console_sink == nullptr) [[unlikely]]
        {
            return;     // 尚未创建：阈值在创建时生效
        }

        m_console_sink->set_level(Detail::to_spdlog_level(level));
        sync_backend_level();
    }

    void Logger::apply_file_level(Level level) noexcept
    {
        m_file_level = level;

        if (m_file_sink == nullptr) [[unlikely]]
        {
            return;     // 尚未创建：阈值在创建时生效
        }

        m_file_sink->set_level(Detail::to_spdlog_level(level));
        sync_backend_level();
    }
} // namespace ID::Log
