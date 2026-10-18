#pragma once

#include <atomic>

#include "Common/Core.hpp"
#include "Common/templates/Singleton.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  微内核自举日志系统（BootstrapLog）
 *  在 ID.Log 正式日志插件加载前提供零堆内存分配的底层日志与断言输出能力；
 *  当 ID.Log 插件加载后，可通过 set_sink() 原子热替换接管所有内核日志。
 * 
 *  sink：接收器。一个日志系统可以有多个 sink，每个 sink 负责将日志输出到不同的目标，比如 ConsoleSink 用于输出到控制台、 FileSink 用于输出到文件、NetworkSink 用于通过网络发送日志等。
 */

namespace ID::Log
{
    enum class Level : uint8_t
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,      // 明确的错误，约定静默返回
        Fatal       // 致命错误，约定直接终止进程
    };

    /**
     *  @brief 日志接收器函数类型定义。该函数类型用于定义日志接收器的接口，允许用户自定义日志处理逻辑。
     *  @param level 日志级别，表示日志的严重程度，见 enum class ID::Log::Level
     *  @param category 日志所属的 模块/类别 的标签，例如 "ID.Kernel"、"ID.Renderer" 等。
     *  @param message 日志消息内容，通常是格式化后的字符串
     *  @note 实际使用时，可以根据 level、category 进行过滤、检索等操作
     */
    using SinkFn = void(*)(Level level, std::string_view category, std::string_view message) noexcept;

    /**
     *  @brief 默认的自举日志接收器函数。该函数在 ID.Log 插件加载前作为默认的日志处理逻辑。
     *  @param level 日志级别，表示日志的严重程度
     *  @param category 日志所属的 模块/类别 的标签，例如 "ID.Kernel"、"ID.Renderer" 等
     *  @param message 日志消息内容，通常是格式化后的字符串
     *  @note 该函数会将日志输出到标准错误流（stderr），并在日志级别为 Fatal 时终止程序执行。
     */
    ID_KERNEL_API void default_bootstrap_sink(Level level, std::string_view category, std::string_view message) noexcept;

    /**
     *  自举日志类，确保其只有唯一实例，并提供线程安全的日志接收器设置与获取接口。
     *  @note TODO
     */
    class ID_KERNEL_API BootstrapLog : public SingletonEager<BootstrapLog>
    {
        friend class SingletonEager<BootstrapLog>;

    protected:
        constexpr BootstrapLog() noexcept = default;
        ~BootstrapLog() noexcept = default;

    public:
        /**
         *  @brief 设置当前激活的日志接收器函数
         *  @param new_sink 新的日志接收器函数指针，建议不要传入 nullptr
         *  @note 如果传入的 new_sink 为 nullptr，则会断言失败，直接导致程序终止
         */
        ID_FORCEINLINE void set_sink(SinkFn new_sink) noexcept
        {
            if (new_sink == nullptr) [[unlikely]]
            {
                report_assert("new_sink != nullptr", "BootstrapLog::set_sink() received nullptr, resetting to default_bootstrap_sink", __FILE__, __LINE__);
            }

            m_active_sink.store(new_sink, std::memory_order_release);
        }

        /**
         *  @brief 获取当前激活的日志接收器函数
         *  @return 返回当前激活的日志接收器函数指针
         *  @note 此过程为原子操作，线程安全
         */
        ID_FORCEINLINE SinkFn get_sink() const noexcept
        {
            return m_active_sink.load(std::memory_order_acquire);
        }

        /**
         *  @brief 报告日志消息
         *  @param level 日志级别，表示日志的严重程度
         *  @param category 日志所属的 模块/类别 的标签，例如 "ID.Kernel"、"ID.Renderer" 等
         *  @param message 日志消息内容，通常是格式化后的字符串
         *  @note 此函数会调用当前激活的日志接收器函数来处理日志消息
         */
        ID_NOINLINE ID_COLD void report(Level level, std::string_view category, std::string_view message) const noexcept;

        /**
         *  @brief 报告断言失败信息
         *  @param expr 断言表达式的字符串表示
         *  @param message 断言失败的附加消息
         *  @param file 断言失败的源文件名
         *  @param line 断言失败的源文件行号
         *  @note 此函数会调用当前激活的日志接收器函数来处理断言失败信息，并在必要时终止程序执行
         */
        ID_NOINLINE ID_COLD void report_assert(std::string_view expr, std::string_view message, std::string_view file, uint32_t line) const noexcept;

    private:
        // BootstrapLog 的唯一实例
        static BootstrapLog s_instance;

        // 原子指针，指向当前激活的日志接收器函数，默认为 default_bootstrap_sink
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
