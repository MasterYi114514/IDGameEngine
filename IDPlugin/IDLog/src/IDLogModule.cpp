#include "IDLogLifecycle.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "Kernel/PluginRuntime.hpp"

#include "IDLogLogger.hpp"
#include "SpdlogFrontend.hpp"

/**
 *  ID.Log 插件实现：四阶段接线 + 服务表 + Logger 槽位 + BootstrapLog 桥
 *  1. `on_register` 只发布服务表：不建 logger、不建线程、不构造任何 spdlog 对象；
 *  2. `on_init` 先建前端，**成功之后**才把内核自举日志接到 `bridge_sink`；失败路径不留已接管的 sink；
 *  3. `on_shutdown` 必须**先**把 `BootstrapLog` 交回默认 sink，**再**释放前端：
 *     反序会让内核日志的 `bridge_sink` 悬空（DLL 内函数指针 + 已销毁的前端）；
 *     统计读数必须在 `shutdown()` 之前取，后端销毁后其线程池计数随之消失；
 *     最后复位 `Logger` 槽位与游标——句柄在 shutdown 后即失效，复位由插件单方决定；
 *  4. `on_unload` 只注销自己的槽位；
 *  5. 本文件不含任何 spdlog 类型：前端是 pimpl，spdlog 只出现在 `SpdlogFrontend.cpp`。
 */

namespace ID::Log
{
    namespace
    {
        // ---- 前向声明：服务表要先引用这些函数，函数体写在表之后 ----
        void    write_entry(uint32_t level, const char* category, uint32_t category_len,
                            const char* message, uint32_t message_len) noexcept;
        bool    configure_entry(const LogConfig& config) noexcept;
        void    flush_entry() noexcept;
        Logger* acquire_entry(const char* category, uint32_t category_len) noexcept;
        bool    reconfigure_entry(const LogConfig& config) noexcept;
        void    get_stats_entry(LogStats* stats) noexcept;

        // ---- 服务表：静态存储期 + constexpr 初始化，地址在进程内不变 ----
        // `&g_active_level` 是常量表达式（静态存储期对象的外部链接地址），因此整张表可以 constexpr。
        constexpr LogApiTable g_log_api_table
        {
            .write          = &write_entry,
            .configure      = &configure_entry,
            .flush          = &flush_entry,
            .acquire_logger = &acquire_entry,
            .reconfigure    = &reconfigure_entry,
            .get_stats      = &get_stats_entry,
            .active_level   = &g_active_level,
            .revision       = kLogApiRevision,
        };

        // 前端实例：常量初始化，零运行期全局构造
        constinit SpdlogFrontend g_frontend;

        // acquire_logger 的固定槽位：冷路径、无锁、不查重，耗尽即返回 nullptr
        constinit Logger                 g_logger_slots[kMaxLoggerHandles]{};
        constinit std::atomic<uint32_t>  g_logger_cursor{ 0 };

        // 内核日志是否已被本插件接管：`on_init` 接到 `bridge_sink` 之后置位，`on_shutdown` 断开后清零。
        // 存在理由：`on_init` 返回 false 时 runtime 仍会逆序调用 `on_shutdown`（已尝试 init 的前缀包含
        // 失败者），那一轮既没接管 sink 也没建前端，收尾诊断必须按"从未接管"处理。
        constinit bool g_sink_attached = false;

        /**
         *  @brief 冷路径：把句柄槽位与游标复位到"从未申请"的状态
         *  @note 句柄只在 Ready 区间内有效，`on_shutdown` 之后一律作废（下游必须置空），
         *        因此复位由插件**单方决定**：不通知下游、不等待引用，直接清空槽内容并归零游标。
         *        没有这一步，每轮 `start`/`shutdown` 都会线性消耗槽位，64 轮之后 `acquire_logger`
         *        永远返回 nullptr。复位后槽与游标回到"未申请"状态：旧句柄的 `table` 变成 nullptr，
         *        在下一轮重新发出该槽之前使用它会空指针解引用（发出之后则别名到新槽）——
         *        越界使用句柄本来就不属于契约，这里只保证不留下上一轮的 `category` 指针。
         */
        void reset_logger_slots() noexcept
        {
            for (uint32_t i = 0; i < kMaxLoggerHandles; ++i)
            {
                g_logger_slots[i] = Logger{};
            }
            g_logger_cursor.store(0, std::memory_order_relaxed);
        }

        /**
         *  @brief 把一段可能为空/零长的字节只读地写进 stream
         *  @param stream 目标 stream
         *  @param data 数据指针；允许 nullptr，不要求 NUL 结尾
         *  @param len 字节数；0 或 `data` 为空时什么都不写
         *  @note 只做 `fwrite`：不格式化、不分配、不读断言路径以外的状态
         */
        void write_raw(std::FILE* stream, const char* data, uint32_t len) noexcept
        {
            if (data != nullptr && len != 0)
            {
                std::fwrite(data, 1, len, stream);
            }
        }

        /**
         *  @brief 写一个字符串字面量（长度由数组类型给出，不调用 strlen、不落魔数）
         *  @param stream 目标 stream
         *  @param text 字面量；末尾 NUL 不计入写入
         */
        template <std::size_t N>
        void write_literal(std::FILE* stream, const char (&text)[N]) noexcept
        {
            std::fwrite(text, 1, N - 1, stream);
        }

        /**
         *  @brief 自举日志桥：把内核日志转交给 IDLog 前端
         *  @param level 内核日志级别（`BootstrapLog` 的 `Level` 与本插件同源）
         *  @param category 内核给出的分类标签
         *  @param message 内核给出的消息（已格式化）
         *  @note 线程局部重入标志：spdlog 内部若再触发内核日志，直接丢弃，
         *        **绝不递归回 `BootstrapLog`**（否则会在 sink 内自我递归）。
         *        唯一例外是重入时的 `Fatal`：它可能来自"前端不可用 → `fatal_without_backend`
         *        → `BootstrapLog::report(Fatal)`"这条链，此时这一行是进程的最后一条输出，
         *        静默丢弃会让 Fatal 什么都不打印就终止。该分支把 category/message 只读地写到
         *        stderr（不格式化、不分配、不回 `BootstrapLog`）后终止。
         */
        void bridge_sink(Level level, std::string_view category, std::string_view message) noexcept
        {
            static thread_local bool s_in_bridge = false;
            if (s_in_bridge)
            {
                if (level != Level::Fatal)
                {
                    return;   // 重入的普通日志：丢弃，绝不递归
                }

                // 重入的 Fatal：不能再进 sink（会第三次回到本函数），直接把最后一行写到 stderr。
                // category/message 允许为空或零长，因此逐段判空，不做任何格式化
                write_literal(stderr, "[FATAL] ");
                if (!category.empty())
                {
                    write_raw(stderr, category.data(), static_cast<uint32_t>(category.size()));
                    write_literal(stderr, ": ");
                }
                write_raw(stderr, message.data(), static_cast<uint32_t>(message.size()));
                write_literal(stderr, "\n");
                std::fflush(stderr);
                std::abort();
            }
            s_in_bridge = true;

            g_frontend.write(static_cast<uint32_t>(level),
                             category.data(), static_cast<uint32_t>(category.size()),
                             message.data(), static_cast<uint32_t>(message.size()));

            s_in_bridge = false;
        }

        void write_entry(uint32_t level, const char* category, uint32_t category_len,
                         const char* message, uint32_t message_len) noexcept
        {
            // 不做级别收敛：前端 `write` 是唯一收敛点，`LogApiTable::write` 与内核 bridge 都经过它，
            // 两处各收敛一次只会让"谁负责过滤"变成两个真源
            g_frontend.write(level, category, category_len, message, message_len);
        }

        bool configure_entry(const LogConfig& config) noexcept
        {
            return g_frontend.configure(config);
        }

        void flush_entry() noexcept
        {
            g_frontend.flush();
        }

        Logger* acquire_entry(const char* category, uint32_t category_len) noexcept
        {
            if (category == nullptr)
            {
                return nullptr;
            }

            // relaxed 递增即可：槽位只要求互不相同，不承担任何同步语义
            const uint32_t index = g_logger_cursor.fetch_add(1, std::memory_order_relaxed);
            if (index >= kMaxLoggerHandles)
            {
                return nullptr;
            }

            Logger& slot = g_logger_slots[index];
            slot.table        = &g_log_api_table;
            slot.category     = category;
            slot.category_len = category_len;
            return &slot;
        }

        bool reconfigure_entry(const LogConfig& config) noexcept
        {
            return g_frontend.reconfigure(config);
        }

        void get_stats_entry(LogStats* stats) noexcept
        {
            g_frontend.get_stats(stats);
        }
    } // namespace

    extern "C" void IDLogOnRegister() noexcept
    {
        // 只发布表：spdlog 的任何构造都不得出现在本阶段，也不得出现在静态初始化里
        PluginRuntime::instance().publish_service(kLogSlot, &g_log_api_table);
    }

    extern "C" bool IDLogOnInit() noexcept
    {
        // 配置文件与环境变量的读取不属于本插件：宿主通过 configure()/reconfigure() 表达意图，
        // 这里用默认配置把后端建起来，让 Ready 区间一开始就有可用的 sink 与队列
        LogConfig config{};
        if (!g_frontend.init(config))
        {
            return false;   // 失败即返回：此时还没有接管内核日志，不存在需要回退的 sink
        }

        // 建立 sink 成功之后才接管内核日志，顺序不可交换
        BootstrapLog::instance().set_sink(&bridge_sink);
        g_sink_attached = true;

        // 这一行是内核日志（category = "ID.Kernel"），它会经桥走一遍完整链路，
        // 因此控制台能直接证明 sink 接管生效
        ID_KERNEL_LOG(Level::Info, "ID.Log attached: kernel log now flows through the spdlog frontend");

        Logger* const logger = acquire_logger(g_log_api_table, "ID.Log");
        if (logger != nullptr)
        {
            logger->info("ID.Log ready: revision={}, mode={}, queue_size={}, handle_capacity={}",
                         kLogApiRevision,
                         config.async ? "async" : "sync",
                         config.queue_size,
                         kMaxLoggerHandles);
        }
        return true;
    }

    extern "C" void IDLogOnShutdown() noexcept
    {
        // `on_init` 失败时 runtime 仍会逆序调用本回调（见 PluginRuntime::rollback：已尝试 init 的前缀
        // 包含返回 false 的那个插件），因此这里不能假设上一轮接管过 sink
        const bool was_attached = g_sink_attached;

        // 顺序不可交换：先把内核日志交回默认 sink，再销毁前端；
        // 反之内核日志会调进已释放的 bridge_sink 目标。未接管时两者都是安全的空动作
        BootstrapLog::instance().set_sink(&default_bootstrap_sink);
        g_sink_attached = false;   // 断开之后才清零：标志的作用域与"本插件是否持有内核日志"严格同步

        // 统计必须在 shutdown() 之前读：后端销毁后，线程池的 overrun/discard 计数随之消失。
        // 此刻内核日志已交回默认 sink，因此这一行不会有日志经桥回流进正在收敛的前端
        LogStats stats{};
        g_frontend.get_stats(&stats);

        g_frontend.shutdown();

        // 句柄已随 shutdown 作废，复位槽位让下一轮 start 重新拿到全部 64 个槽位
        reset_logger_slots();

        // 只有确实接放过 sink 才有"断开"这回事：这一行走默认自举 sink（stderr），
        // 同时证明断开之后内核日志仍然可用且不悬空。回滚路径（on_init 失败）不得打印它
        if (was_attached)
        {
            char buffer[320];
            std::snprintf(buffer, sizeof(buffer),
                          "ID.Log detached: overrun=%llu discarded=%llu pre_ready=%llu post_shutdown=%llu "
                          "active_level=%u queue_capacity=%u",
                          static_cast<unsigned long long>(stats.overrun_dropped),
                          static_cast<unsigned long long>(stats.discarded),
                          static_cast<unsigned long long>(stats.dropped_before_ready),
                          static_cast<unsigned long long>(stats.dropped_after_shutdown),
                          stats.active_level,
                          stats.queue_capacity);
            ID_KERNEL_LOG(Level::Info, buffer);
        }
    }

    extern "C" void IDLogOnUnload() noexcept
    {
        PluginRuntime::instance().revoke_service(kLogSlot);
    }
} // namespace ID::Log
