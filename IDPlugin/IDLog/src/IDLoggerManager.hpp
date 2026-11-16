#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "IDLog.hpp"
#include "IDLogger.hpp"
#include "Singleton.hpp"

/**
 *  日志器登记与生命周期管理（IDLog.dll 内部实现）。
 *  
 *  不变量：
 *  1. 每个 `LoggerEntry` 的槽位下标就是对外句柄的 id；条目表只增长、不压缩，句柄下标因此不会被搬移，
 *     也不会因其它日志器的销毁而改变。
 *  2. 条目表的元素地址在一次 `init` / `shutdown` 生命周期内**稳定**：追加新条目不会使已取出元素的
 *     地址失效（`std::deque` 在两端插入只失效迭代器，对元素的引用与指针保持有效），因此
 *     `get_logger` 返回的 `Logger*` 不会被其它日志器的 `create_logger` / `destroy_logger` 悬垂。
 *  3. “条目存活”由 `LoggerEntry::is_alive()` 判定：条目被 `destroy()` 后其内部持有被置空，
 *     陈旧句柄再取会得到 nullptr，不会被当作可用对象；条目本身仍留在表内并被清空（销毁后的旧
 *     `Logger*` 上日志调用不产生输出、四个配置方法返回 false）。空闲下标被后续 `create_logger`
 *     复用时以就地移动赋值替换条目内容、不替换对象，同一地址指向新日志器（ABA）。
 *  4. 登记结构由 `m_mutex` 保护；日志写入路径不经过该锁。
 *  5. 不注册 spdlog 全局 registry，也不使用默认 logger：全部对象由本管理器实例独占。
 *  6. “已初始化”的判定在 `create` 里出现两次：锁外一次只是免去必然失败时的构造代价，插入前在
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
        /**
         *  @brief 地址稳定的条目表：下标即句柄 id，元素地址在追加后保持不变
         *  @note 容器选 `std::deque` 而不是 `std::vector`：`std::deque` 在两端插入只失效迭代器，
         *        对元素的引用与指针保持有效（标准保证），因此已交给调用方的 `Logger*` 不会因为
         *        其它日志器的登记操作而悬垂。本表只在登记类冷路径上被取用，写日志路径不经过它。
         *  @note 槽位语义：`destroy` 保留条目并清空其内容、下标回归空闲表；`create` 优先复用空闲下标且
         *        **就地赋值、不替换对象**（同一地址指向新日志器）；`clear` 销毁全部内容并把下标复位到
         *        0（跨生命周期 ABA）。
         */
        class LoggerSlotTable
        {
        public:
            /**
             *  @brief 取得一个槽位并放入条目
             *  @param entry 被移入槽位的条目；调用后处于有效但未指定状态
             *  @return 槽位下标，可作为句柄 id 保存
             *  @note 可能分配（空闲表为空时追加 `m_entries`）；分配失败以异常传播给调用方
             */
            std::uint32_t create(LoggerEntry&& entry);

            /**
             *  @brief 销毁槽位内容并回收下标（条目对象本身保留在表内）
             *  @param index 槽位下标；越界时无操作
             *  @note 先调用 `LoggerEntry::destroy()` 完成清空，再把下标压入空闲表
             *  @note 可能分配（空闲表扩容）；分配失败时吞掉异常、丢弃该下标（槽位不复用），
             *        条目已清空因此不会泄漏，本函数也不会向调用方抛出
             */
            void destroy(std::uint32_t index) noexcept;

            /**
             *  @brief 按下标取条目
             *  @return 越界时返回 nullptr；不判定条目是否存活
             */
            LoggerEntry* try_get(std::uint32_t index) noexcept;
            const LoggerEntry* try_get(std::uint32_t index) const noexcept;

            std::size_t size() const noexcept { return m_entries.size(); }

            /**
             *  @brief 销毁全部条目内容并清空表（下标从 0 重新开始）
             *  @note 逐个调用 `LoggerEntry::destroy()`；要求该调用可重复（对已销毁条目是空操作）
             */
            void clear() noexcept;

        private:
            std::deque<LoggerEntry>     m_entries;          // 条目表：追加不失效已有元素的地址
            std::vector<std::uint32_t>  m_freed_indices;    // 空闲下标栈，尾部为最近归还项
        };

        LoggerManager() = default;
        ~LoggerManager() = default;

        std::mutex           m_mutex;                            // 只保护登记结构
        LoggerSlotTable      m_loggers;                          // 地址稳定的条目表，下标即句柄 id
        std::atomic<bool>    m_is_initialized{ false };          // 就绪标志，release/acquire 配对
    };
} // namespace ID::Log
