#pragma once

#include <atomic>

#include "Common/Core.hpp"
#include "Common/templates/Singleton.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  微内核自举日志系统（BootstrapLog）
 *  在 ID.Log 正式日志插件加载前提供零堆内存分配的底层日志与断言输出能力；
 *  当 ID.Log 插件加载后，可通过 set_sink() 原子热替换接管所有内核日志。
 */

namespace ID::Log
{
    enum class Level : uint8_t
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Fatal
    };

    // 接收器函数类型定义
    using SinkFn = void(*)(Level level, std::string_view category, std::string_view message) noexcept;

    // 默认自举输出函数声明（全程栈内存操作，零堆分配，支持 constexpr 绑定）
    ID_KERNEL_API void default_bootstrap_sink(Level level, std::string_view category, std::string_view message) noexcept;

    // 单例模式实现的日志系统
    class ID_KERNEL_API BootstrapLog : public SingletonEager<BootstrapLog>
    {
        friend class SingletonEager<BootstrapLog>;

    protected:
        constexpr BootstrapLog() noexcept = default;
        ~BootstrapLog() noexcept = default;

    public:
        // 原子设置新的日志接收器（供 ID.Log 插件在 OnRegister 阶段无缝接管）
        ID_FORCEINLINE void set_sink(SinkFn new_sink) noexcept
        {
            m_active_sink.store(new_sink ? new_sink : &default_bootstrap_sink, std::memory_order_release);
        }

        // 原子获取当前激活的日志接收器
        ID_FORCEINLINE SinkFn get_sink() const noexcept
        {
            return m_active_sink.load(std::memory_order_acquire);
        }

        // 标记为 ID_NOINLINE，将日志派发指令剥离出热循环，避免污染 CPU L1 指令缓存 (I-Cache)
        ID_NOINLINE void report(Level level, std::string_view category, std::string_view message) const noexcept;

        // 断言失败报告（冷路径）
        ID_NOINLINE void report_assert(std::string_view expr, std::string_view message, std::string_view file, uint32_t line) const noexcept;

    private:
        static BootstrapLog s_instance;             // 唯一实例
        std::atomic<SinkFn> m_active_sink{ &default_bootstrap_sink };
    };
} // namespace ID::Log

// 微内核日志与断言宏
#define ID_KERNEL_LOG(lvl, msg) \
    ::ID::Log::BootstrapLog::instance().report((lvl), "ID.Kernel", (msg))

#define ID_KERNEL_ASSERT(expr, msg)                                                               \
    do                                                                                            \
    {                                                                                             \
        if (!(expr)) [[unlikely]]                                                                 \
        {                                                                                         \
            ::ID::Log::BootstrapLog::instance().report_assert(#expr, (msg), __FILE__, __LINE__);  \
            ID_DEBUG_BREAK();                                                                     \
        }                                                                                         \
    } while (false)
