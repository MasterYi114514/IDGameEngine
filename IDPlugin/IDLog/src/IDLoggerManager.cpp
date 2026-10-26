#include "IDLoggerManager.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/logger.h>

namespace ID::Log
{
    void LoggerEntry::destroy() noexcept
    {
        flush();

        logger.m_console_sink.reset();
        logger.m_file_sink.reset();
        logger.m_backend.reset();
        logger.m_file_path.clear();
    }

    bool LoggerEntry::is_alive() const noexcept
    {
        return logger.m_backend != nullptr;
    }

    void LoggerEntry::flush() noexcept
    {
        if (logger.m_backend == nullptr) [[unlikely]]
        {
            return;
        }

        try
        {
            logger.m_backend->flush();
        }
        catch (...)
        {
            // 销毁与关闭路径不把 IO 失败变成异常或终止
        }
    }

    void LoggerManager::initialize() noexcept
    {
        // 初始化本身不创建全局对象：spdlog 默认 logger、registry 与线程池一概不使用。
        // 该标志是“就绪”的发布点：release 与 create 锁外快路径的 acquire 配对，使已串行完成 init 的
        // 线程在任何平台上都能看到 true（权威判定仍在锁内，见 create）。
        m_is_initialized.store(true, std::memory_order_release);
    }

    void LoggerManager::shutdown() noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        // 先 flush 全部存活条目，再销毁：文件目标的缓冲到此为止已交付给文件流
        for (std::size_t index = 0; index < m_loggers.size(); ++index)
        {
            LoggerEntry* entry = m_loggers.try_get(static_cast<std::uint32_t>(index));
            if (entry != nullptr)
            {
                entry->flush();
            }
        }

        m_loggers.clear();

        // 与 initialize 的 release 对称：把“就绪”撤回，配对 create 快路径的 acquire
        m_is_initialized.store(false, std::memory_order_release);
    }

    LoggerHandle LoggerManager::create(const char* name)
    {
        // 锁外快路径：未初始化时直接失败，不必付出名称拷贝与后端构造的代价。
        // acquire 与 initialize 的 release 配对；本判定只是快路径，权威判定在下面锁内。
        if (!m_is_initialized.load(std::memory_order_acquire)) [[unlikely]]
        {
            return LoggerHandle::invalid_handle();
        }

        const std::string logger_name = (name != nullptr) ? std::string(name) : std::string{};

        std::shared_ptr<spdlog::logger> backend = std::make_shared<spdlog::logger>(logger_name);
        backend->set_level(spdlog::level::trace);
        backend->flush_on(spdlog::level::err);

        LoggerEntry entry{ Logger(std::move(backend)) };

        // 默认配置：终端启用（阈值 Trace），文件停用；两者由 Logger 的配置入口统一切换
        entry.logger.enable_sink(SinkType::Console);

        // 上面的构造全在锁外：这段窗口里可能有其它线程完成 shutdown()，把登记表清空并把状态复位
        const std::lock_guard<std::mutex> lock(m_mutex);

        // 插入前在锁内重查：必须确认仍处于已初始化状态。没有这一步，条目会被插进一个已经关闭的
        // 登记表——shutdown() 的 flush 与销毁循环已经走完，该条目不会再被清理，句柄也会在“已关闭”
        // 状态下返回给调用方。以无效句柄失败返回，才使 shutdown 与登记类接口的互斥契约成立。
        // 这里用 relaxed：与 shutdown 的读写已经在同一把锁下，互斥本身提供顺序。
        if (!m_is_initialized.load(std::memory_order_relaxed)) [[unlikely]]
        {
            // 未登记：本地 entry 随返回析构，刚构造的后端与 sink 一并释放
            return LoggerHandle::invalid_handle();
        }

        return Logger::make_handle(m_loggers.create(std::move(entry)));
    }

    Logger* LoggerManager::get(LoggerHandle handle) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        LoggerEntry* entry = m_loggers.try_get(handle.get_id());
        if (entry == nullptr || !entry->is_alive()) [[unlikely]]
        {
            return nullptr;
        }

        return &entry->logger;
    }

    void LoggerManager::destroy(LoggerHandle handle) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        LoggerEntry* entry = m_loggers.try_get(handle.get_id());
        if (entry == nullptr || !entry->is_alive()) [[unlikely]]
        {
            return;     // 无效句柄或已销毁句柄：不重复回收槽位
        }

        m_loggers.destroy(handle.get_id());
    }
} // namespace ID::Log
