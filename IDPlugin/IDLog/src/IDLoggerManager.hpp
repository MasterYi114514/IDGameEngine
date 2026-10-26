#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "IDLog.hpp"
#include "IDLogger.hpp"
#include "Singleton.hpp"
#include "SlotPool.hpp"

/**
 *  日志器登记与生命周期管理（IDLog.dll 内部实现）。
 *  
 *  不变量：
 *  1. 每个 `LoggerEntry` 的槽位下标就是对外句柄的 id；槽位数组只增长，句柄因此不会被压缩搬移。
 *  2. “条目存活”由 `LoggerEntry::is_alive()` 判定：条目被 `destroy()` 后其内部持有被置空，
 *     陈旧句柄再取会得到 nullptr，不会被当作可用对象。
 *  3. 登记结构由 `m_mutex` 保护；日志写入路径不经过该锁。
 *  4. 不注册 spdlog 全局 registry，也不使用默认 logger：全部对象由本管理器实例独占。
 *  5. “已初始化”的判定在 `create` 里出现两次：锁外一次只是免去必然失败时的构造代价，插入前在
 *     `m_mutex` 内必须重查。`shutdown` 持同一把锁清空登记表并复位状态，因此“构造已完成、关闭已
 *     发生”的条目不会被登记，而是以无效句柄返回——这是 shutdown 与登记类接口互斥的实现依据。
 */

namespace ID::Log
{
    /**
     *  @brief 池条目：一个槽位的存活对象
     */
    struct LoggerEntry
    {
        Logger  logger;     // 对外可见的日志器对象；其内部持有后端 logger 与 sink

        /**
         *  @brief 冲刷后释放内部持有，使条目变为“死亡”
         *  @note 可重复调用；flush 失败被吞掉（销毁路径不抛异常）
         */
        void destroy() noexcept;

        /**
         *  @brief 条目是否仍持有可用后端
         */
        bool is_alive() const noexcept;

        /**
         *  @brief 冲刷后端持有（失败被吞掉）
         */
        void flush() noexcept;
    };

    class LoggerManager : public ID::Common::SingletonLazy<LoggerManager>
    {
        friend class ID::Common::SingletonLazy<LoggerManager>;

    public:
        void initialize() noexcept;
        void shutdown() noexcept;

        bool is_initialized() const noexcept { return m_is_initialized.load(std::memory_order_relaxed); }

        LoggerHandle create(const char* name);
        Logger* get(LoggerHandle handle) noexcept;
        void destroy(LoggerHandle handle) noexcept;

    private:
        LoggerManager() = default;
        ~LoggerManager() = default;

        using LoggerPool = ID::Common::SlotPool<std::uint32_t, LoggerEntry>;

        std::mutex          m_mutex;                            // 只保护登记结构
        LoggerPool          m_loggers;
        std::atomic<bool>   m_is_initialized{ false };
    };
} // namespace ID::Log
