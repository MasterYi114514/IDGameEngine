#pragma once

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>

#include "IDLogApi.hpp"

/**
 *  ID.Log 的调用侧外观（**唯一包含 `<format>` 的公开头**）
 *  1. `Logger` 是可平凡复制的纯数据（`table` / `category` / `category_len` 三个 public 字段），
 *     由插件在固定槽位内分配、调用方内联读字段；跨 DLL 只传它的指针；
 *  2. 格式化在**调用方**完成：`std::format_to_n` 写进 `kStackFormatCapacity` 栈缓冲，不分配、可重入；
 *     截断时收敛到 UTF-8 码点边界并补 "..."；插件侧不再解析格式串，只做 `[category] message` 拼接；
 *  3. 格式串形参固定为 `std::format_string<Args...>`（构造函数是 `consteval`），
 *     非字面量、占位符数量或类型不匹配一律编译期报错；**不提供 `std::vformat` 这类运行期格式串入口**；
 *  4. 编译期底线是 `ID_LOG_MIN_LEVEL`（由 `IDLogApi.hpp` 固定，不可由构建覆盖）：
 *     `Logger` 的固定级别方法与固定级别宏共用同一条底线：方法用 `if constexpr` 裁掉方法体，
 *     宏用 `#if` 在预处理期展开为 `((void)0)`，实参完全不求值；方法形态是函数调用，
 *     **实参在进入方法前必然已求值**，只有方法体被裁掉；
 *  5. `Level::Fatal` 不参与普通裁剪、不受 `active_level` 过滤：输出后一定终止进程；
 *     句柄为空、句柄的表为空或后端不可用时走 `detail::fatal_fallback`，同样终止；
 *  6. 读侧零检查：`Logger` 的普通方法不判空、不加锁、不查表，句柄必须来自 `acquire_logger`
 *     且只在 Ready 区间内使用；默认构造或跨 shutdown 保留的句柄调用即空指针解引用。
 */

namespace ID::Log
{
    namespace detail
    {
        /**
         *  @brief 消费掉被编译期裁剪掉的形参，避免 `-Wunused-parameter`
         *  @param ... 任意形参
         *  @note 这是普通函数调用：它只消除未使用告警，**不会**阻止实参求值。
         *        真正让实参不求值的是宏在编译底线以下展开成 `((void)0)`。
         */
        template <typename... Args>
        constexpr void log_unused(Args&&...) noexcept
        {
        }

        /**
         *  @brief 编译期裁剪判定：该级别是否达到 `ID_LOG_MIN_LEVEL`
         *  @param level 级别数值，取 `static_cast<int>(Level::X)` 或运行期数值
         *  @return 达到（不会被裁剪）时为 true
         *  @note 比较放在函数里而不是直接写在 `if constexpr` 上，是因为 GCC 对
         *        `static_cast<int>(枚举常量) >= 0` 会报 `-Wtype-limits`（枚举的值域被折叠成单点）。
         *        函数形参的值域是完整 int，因此 `-Wall -Wextra` 下零告警；语义与内联写法完全一致。
         *        宏与 `Logger` 的固定级别方法共用本函数，避免两套裁剪语义漂移。
         */
        constexpr bool level_compiled_in(int level) noexcept
        {
            return level >= ID_LOG_MIN_LEVEL;
        }

        /**
         *  @brief Fatal 回退路径的重入标志（线程局部）
         *  @return 引用；true 表示当前线程已经在 Fatal 回退里
         *  @note 做成 inline 函数的函数局部 `thread_local`，而不是模板函数里的 `static`：
         *        后者会让每个实例各持一份标志，重入保护就形同虚设。
         */
        inline bool& fatal_fallback_active() noexcept
        {
            static thread_local bool s_active = false;
            return s_active;
        }

        /**
         *  @brief Fatal 的安全回退：格式化到栈缓冲后交给 `BootstrapLog`，然后终止进程
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         *  @note 用于句柄为空、句柄的表为空或后端不可用三种情形：此时没有可用的 sink，
         *        但 Fatal 的语义是"必须先输出再终止"，因此改走内核自举日志（默认 stderr）。
         *        重入时直接 `fputs` 到 stderr 后 abort：格式化失败或 sink 再次回调本函数都不会递归。
         *        本函数不返回。
         */
        template <typename... Args>
        [[noreturn]] ID_NOINLINE void fatal_fallback(std::format_string<Args...> fmt, Args&&... args) noexcept
        {
            if (fatal_fallback_active())
            {
                std::fputs("[FATAL] ID.Log: fatal fallback re-entered, aborting\n", stderr);
                std::abort();
            }
            fatal_fallback_active() = true;

            char buffer[kStackFormatCapacity];
            const auto formatted = std::format_to_n(
                buffer, static_cast<std::ptrdiff_t>(kStackFormatCapacity) - 1, fmt, std::forward<Args>(args)...);
            const uint32_t used = mark_truncated(buffer, kStackFormatCapacity, static_cast<uint32_t>(formatted.size));

            BootstrapLog::instance().report(Level::Fatal, "ID.Log", std::string_view(buffer, used));
            std::abort();
        }
    } // namespace detail

    struct Logger
    {
        const LogApiTable* table        = nullptr;    // 发布到 ServiceSlot::Log 的表；Ready 区间内有效
        const char*        category     = nullptr;    // category 标签；调用方保证在句柄有效期内不释放
        uint32_t           category_len = 0;          // category 字节数（不含 NUL）

        /**
         *  @brief 该句柄当前是否允许输出 level（运行期预检：一次 relaxed load + 一次分支）
         *  @param level 目标级别
         *  @return 关闭时返回 false
         *  @note 读侧零检查：不判空、不加锁、不查字符串；`table->active_level` 是唯一过滤真源。
         *        `Fatal` 不经过本判定（见 `fatal`）。
         */
        ID_FORCEINLINE bool is_enabled(Level level) const noexcept
        {
            return static_cast<uint32_t>(level) >= table->active_level->load(std::memory_order_relaxed);
        }

        /**
         *  @brief 格式化一条消息并提交给插件（打开级别的热路径）
         *  @param level 目标级别
         *  @param fmt 编译期格式串 `std::format_string<Args...>`
         *  @param args 格式实参，按引用转发给 `std::format_to_n`
         *  @note 截断时由 `mark_truncated` 收敛到 UTF-8 码点边界并补 "..."，绝不越界写栈缓冲；
         *        标 `ID_NOINLINE` 让调用点只留一次调用指令，避免每个调用点各自展开一整套格式解析。
         *        本入口不做运行期级别过滤，过滤在 `Logger` 固定级别方法与插件内完成。
         */
        template <typename... Args>
        ID_NOINLINE void write_at(Level level, std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            char buffer[kStackFormatCapacity];
            const auto formatted = std::format_to_n(
                buffer, static_cast<std::ptrdiff_t>(kStackFormatCapacity) - 1, fmt, std::forward<Args>(args)...);
            const uint32_t used = detail::mark_truncated(
                buffer, kStackFormatCapacity, static_cast<uint32_t>(formatted.size));

            table->write(static_cast<uint32_t>(level), category, category_len, buffer, used);
        }

        /**
         *  @brief 输出 Trace 级别日志
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         *  @note `ID_LOG_MIN_LEVEL` 高于该级别时方法体整段在编译期删除，但实参在调用前已求值；
         *        底部未裁剪时先做运行期预检再格式化。
         */
        template <typename... Args>
        void trace(std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            if constexpr (detail::level_compiled_in(static_cast<int>(Level::Trace)))
            {
                if (is_enabled(Level::Trace))
                {
                    write_at(Level::Trace, fmt, std::forward<Args>(args)...);
                }
            }
            else
            {
                detail::log_unused(fmt, std::forward<Args>(args)...);
            }
        }

        /**
         *  @brief 输出 Debug 级别日志
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         */
        template <typename... Args>
        void debug(std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            if constexpr (detail::level_compiled_in(static_cast<int>(Level::Debug)))
            {
                if (is_enabled(Level::Debug))
                {
                    write_at(Level::Debug, fmt, std::forward<Args>(args)...);
                }
            }
            else
            {
                detail::log_unused(fmt, std::forward<Args>(args)...);
            }
        }

        /**
         *  @brief 输出 Info 级别日志
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         */
        template <typename... Args>
        void info(std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            if constexpr (detail::level_compiled_in(static_cast<int>(Level::Info)))
            {
                if (is_enabled(Level::Info))
                {
                    write_at(Level::Info, fmt, std::forward<Args>(args)...);
                }
            }
            else
            {
                detail::log_unused(fmt, std::forward<Args>(args)...);
            }
        }

        /**
         *  @brief 输出 Warn 级别日志
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         */
        template <typename... Args>
        void warn(std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            if constexpr (detail::level_compiled_in(static_cast<int>(Level::Warn)))
            {
                if (is_enabled(Level::Warn))
                {
                    write_at(Level::Warn, fmt, std::forward<Args>(args)...);
                }
            }
            else
            {
                detail::log_unused(fmt, std::forward<Args>(args)...);
            }
        }

        /**
         *  @brief 输出 Error 级别日志（可恢复失败用这个级别 + 返回值）
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         */
        template <typename... Args>
        void error(std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            if constexpr (detail::level_compiled_in(static_cast<int>(Level::Error)))
            {
                if (is_enabled(Level::Error))
                {
                    write_at(Level::Error, fmt, std::forward<Args>(args)...);
                }
            }
            else
            {
                detail::log_unused(fmt, std::forward<Args>(args)...);
            }
        }

        /**
         *  @brief 输出 Fatal 级别日志：同步输出 + flush 后 `std::abort()`，本方法不返回
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         *  @note 不判 `is_enabled`：Fatal 不受 `active_level` 过滤，也不被编译底线裁剪。
         *        句柄的 `table` 为空（例如 shutdown 之后的旧句柄）时走 `detail::fatal_fallback`，
         *        仍然先输出再终止，而不是静默返回。只在真正不可恢复时使用。
         *        `write_at(Fatal)` 之后由插件负责 flush + abort；本方法末尾的 `std::abort()`
         *        覆盖"表指向替代表、其 write 对 Fatal 直接返回"的情形，保证终止语义不依赖表实现。
         */
        template <typename... Args>
        [[noreturn]] void fatal(std::format_string<Args...> fmt, Args&&... args) const noexcept
        {
            if (table == nullptr)
            {
                detail::fatal_fallback(fmt, std::forward<Args>(args)...);
            }
            write_at(Level::Fatal, fmt, std::forward<Args>(args)...);
            std::abort();
        }
    };

    static_assert(std::is_trivially_copyable_v<Logger>, "Logger 必须是可平凡复制的纯数据（跨 DLL 只传指针）");

    namespace detail
    {
        /**
         *  @brief Fatal 的统一入口：句柄为空、句柄的表为空或后端不可用时走安全回退，否则直接写 Fatal
         *  @param target 目标句柄；允许为 nullptr
         *  @param fmt 编译期格式串
         *  @param args 格式实参
         *  @note 本函数不返回。做成非成员函数而不是在宏里直接调成员，是为了让 `ID_LOG_FATAL(nullptr, …)`
         *        在调用方 TU 里不触发 `-Wnonnull`（GCC 对"已知空指针上调用成员函数"会报警，
         *        即使该分支被 `target != nullptr` 挡住）。句柄非空但 `table` 为空（shutdown 之后的
         *        旧句柄）同样走回退，保证 Fatal 在任何状态下都先输出再终止。
         */
        template <typename... Args>
        [[noreturn]] ID_NOINLINE void fatal_at(const Logger* target, std::format_string<Args...> fmt,
                                               Args&&... args) noexcept
        {
            if (target == nullptr || target->table == nullptr)
            {
                fatal_fallback(fmt, std::forward<Args>(args)...);
            }

            target->write_at(Level::Fatal, fmt, std::forward<Args>(args)...);
            std::abort();   // write_at(Fatal) 经插件后必然终止；这一行只用于满足 [[noreturn]]
        }
    } // namespace detail

    /**
     *  @brief 冷路径：为 category 申请一个 `Logger` 槽位
     *  @tparam N 字符串字面量长度（含末尾 NUL），category 字节数在编译期算出，不调用 strlen
     *  @param table 已发布的函数表（Ready 区间内取得）
     *  @param category 分类标签，必须是编译期字符串字面量
     *  @return 句柄；表项为空或槽位耗尽时返回 nullptr
     *  @note 无锁、无分配、不做字符串查重；同一 category 重复申请得到不同槽位，语义相同。
     */
    template <size_t N>
    Logger* acquire_logger(const LogApiTable& table, const char (&category)[N]) noexcept
    {
        if (table.acquire_logger == nullptr)
        {
            return nullptr;
        }
        return table.acquire_logger(category, static_cast<uint32_t>(N - 1));
    }

    /**
     *  @brief 冷路径：从已有句柄继续为另一个 category 申请槽位（沿用同一个表）
     *  @tparam N 字符串字面量长度（含末尾 NUL）
     *  @param owner 已取得的句柄，只借用它的 `table`
     *  @param category 分类标签，必须是编译期字符串字面量
     *  @return 句柄；owner 没有表或槽位耗尽时返回 nullptr
     */
    template <size_t N>
    Logger* acquire_logger(const Logger& owner, const char (&category)[N]) noexcept
    {
        return (owner.table == nullptr) ? nullptr : acquire_logger(*owner.table, category);
    }
} // namespace ID::Log

/**
 *  `ID_LOG_*` 宏族：实参包在条件分支内，级别关闭时**完全不求值**。
 *  `logger_ptr` 与动态级别的 `level` 各只求值一次（先存进局部量，再判空/判级别/写日志），
 *  因此传带副作用的表达式也是安全的。
 *
 *  三条路径的分工固定，不要互相借用：
 *  1. 固定级别宏（`ID_LOG_TRACE/DEBUG/INFO/WARN/ERROR`）走 `ID_LOG_AT_FIXED` 快路径：
 *     级别的编译期底线已由各自的 `#if ID_LOG_MIN_LEVEL <= N` 在预处理期保证，
 *     所以宏内只剩一次转换、一次判空与一次 `is_enabled(level)`；
 *     不 clamp、不再查编译底线、不做 Fatal 分派；
 *  2. 动态级别 `ID_LOG_AT` 走完整检查：先把越界 `level` 收敛为 `Error`，再**先判 Fatal**——
 *     Fatal 不参与编译底线与 `is_enabled` 过滤，句柄或表为空时经 `detail::fatal_at` 的回退路径
 *     输出后终止（与 `Logger::fatal` / `ID_LOG_FATAL` 同一套语义）；非 Fatal 才走
 *     判空 + `level_compiled_in` + `is_enabled`；
 *  3. `ID_LOG_FATAL` 是独立的必终止路径，不经过上面两条宏的分派。
 *
 *  函数调用形态 `logger->info(...)` 的实参在调用前必然已求值（函数调用语义固有），
 *  参数昂贵或必须严格零求值时用这些宏。
 */

/**
 *  @def ID_LOG_AT_FIXED(logger_ptr, level, ...)
 *  @brief 固定级别宏的快路径：level 必须是编译期枚举常量，且调用点已由 `#if` 证明高于编译底线
 *  @param logger_ptr 指向 `Logger` 的指针（可为 const、可为 nullptr），只求值一次
 *  @param level 编译期级别枚举常量；**不得**传运行期表达式，本宏不 clamp、不查编译底线
 *  @param ... 格式串与实参，只在命中分支求值一次
 *  @note 运行期只保留两项真正的变量：句柄判空与 `is_enabled(level)`（一次 relaxed load + 一次分支）。
 *        句柄的 `table` 为空（Ready 区间外使用）时解引用是契约外后果，与 `Logger` 的固定级别方法一致；
 *        需要"空表也安全"的动态入口用 `ID_LOG_AT`。`Fatal` 不进本宏（见 `ID_LOG_FATAL`）。
 */
#define ID_LOG_AT_FIXED(logger_ptr, level, ...)                                                    \
    do                                                                                             \
    {                                                                                              \
        const ::ID::Log::Logger* const id_log_fixed_target =                                       \
            static_cast<const ::ID::Log::Logger*>(logger_ptr);                                     \
        if (id_log_fixed_target != nullptr && id_log_fixed_target->is_enabled(level))              \
        {                                                                                          \
            id_log_fixed_target->write_at(level, __VA_ARGS__);                                     \
        }                                                                                          \
    } while (false)

#define ID_LOG_AT(logger_ptr, level, ...)                                                          \
    do                                                                                             \
    {                                                                                              \
        const ::ID::Log::Logger* const id_log_at_target =                                          \
            static_cast<const ::ID::Log::Logger*>(logger_ptr);                                     \
        ::ID::uint32_t id_log_at_level = static_cast<::ID::uint32_t>(level);                       \
        if (id_log_at_level > static_cast<::ID::uint32_t>(::ID::Log::Level::Fatal))                \
        {                                                                                          \
            id_log_at_level = static_cast<::ID::uint32_t>(::ID::Log::Level::Error);                \
        }                                                                                          \
        if (id_log_at_level == static_cast<::ID::uint32_t>(::ID::Log::Level::Fatal))               \
        {                                                                                          \
            ::ID::Log::detail::fatal_at(id_log_at_target, __VA_ARGS__);                            \
        }                                                                                          \
        else if (id_log_at_target != nullptr && id_log_at_target->table != nullptr                 \
                 && ::ID::Log::detail::level_compiled_in(static_cast<int>(id_log_at_level))        \
                 && id_log_at_target->is_enabled(static_cast<::ID::Log::Level>(id_log_at_level)))  \
        {                                                                                          \
            id_log_at_target->write_at(static_cast<::ID::Log::Level>(id_log_at_level),             \
                                       __VA_ARGS__);                                               \
        }                                                                                          \
    } while (false)

// 固定级别宏与 Logger 的固定级别方法共用同一个编译期底线：低于底线时整条语句展开为空，
// 实参表达式连求值都不会发生（`((void)0)` 里没有任何形参引用）；达到底线时走 ID_LOG_AT_FIXED
// 快路径——级别已是编译期枚举常量，底线已由下面的 #if 证明，宏内不再重复这两项运行期检查。
#if ID_LOG_MIN_LEVEL <= 0
    #define ID_LOG_TRACE(logger_ptr, ...) ID_LOG_AT_FIXED(logger_ptr, ::ID::Log::Level::Trace, __VA_ARGS__)
#else
    #define ID_LOG_TRACE(logger_ptr, ...) ((void)0)
#endif

#if ID_LOG_MIN_LEVEL <= 1
    #define ID_LOG_DEBUG(logger_ptr, ...) ID_LOG_AT_FIXED(logger_ptr, ::ID::Log::Level::Debug, __VA_ARGS__)
#else
    #define ID_LOG_DEBUG(logger_ptr, ...) ((void)0)
#endif

#if ID_LOG_MIN_LEVEL <= 2
    #define ID_LOG_INFO(logger_ptr, ...)  ID_LOG_AT_FIXED(logger_ptr, ::ID::Log::Level::Info, __VA_ARGS__)
#else
    #define ID_LOG_INFO(logger_ptr, ...)  ((void)0)
#endif

#if ID_LOG_MIN_LEVEL <= 3
    #define ID_LOG_WARN(logger_ptr, ...)  ID_LOG_AT_FIXED(logger_ptr, ::ID::Log::Level::Warn, __VA_ARGS__)
#else
    #define ID_LOG_WARN(logger_ptr, ...)  ((void)0)
#endif

#if ID_LOG_MIN_LEVEL <= 4
    #define ID_LOG_ERROR(logger_ptr, ...) ID_LOG_AT_FIXED(logger_ptr, ::ID::Log::Level::Error, __VA_ARGS__)
#else
    #define ID_LOG_ERROR(logger_ptr, ...) ((void)0)
#endif

/**
 *  Fatal 不受编译底线裁剪、也不受 `active_level` 过滤：它必须无条件终止。
 *  句柄为空或句柄的表为空时走 `detail::fatal_fallback`（`BootstrapLog` 的 Fatal + abort），
 *  因此 `ID_LOG_FATAL(nullptr, ...)` 同样会先输出再终止，而不是静默什么都不做。
 *  `logger_ptr` 只求值一次；分派经非成员入口，避免在调用方 TU 里对空指针调成员函数而触发 `-Wnonnull`。
 */
#define ID_LOG_FATAL(logger_ptr, ...)                                                              \
    ::ID::Log::detail::fatal_at(static_cast<const ::ID::Log::Logger*>(logger_ptr), __VA_ARGS__)
