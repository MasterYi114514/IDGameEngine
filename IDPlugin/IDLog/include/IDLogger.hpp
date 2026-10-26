#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "IDLogCore.hpp"
#include "IDHandle.hpp"
#include "IDLoggerLevel.hpp"
#include "IDLogSink.hpp"

// IDLogCore.hpp 必须先于 vendor 头包含：它集中定义 spdlog 的编译期开关与 Windows 头守卫
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

/**
 *  单个日志器的公开对象。
 *  
 *  设计取舍：
 *  - 底层使用 vendor 内的 spdlog（header-only）。级别方法在消费者翻译单元内实例化，直接调用
 *    底层 logger；不跨 DLL 传函数表，也不产生 fmt 的第二份实现。
 *  - 本类只暴露必要的实现状态（`std::shared_ptr` 持有），不暴露 spdlog 的派生类型，也不把
 *    spdlog 的格式化对象暴露给调用方。
 *  - 消息与模式串按 UTF-8 解释；Windows 终端需要代码页 65001 才能正确显示非 ASCII 字符，
 *    本插件不修改进程级控制台设置。
 *  
 *  线程契约：
 *  - `trace` / `debug` / `info` / `warn` / `error` / `fatal` 可由多线程并发调用（含同一对象）。
 *  - 配置方法（`enable_sink` / `disable_sink` / `set_sink_level` / `set_sink_path`）**非线程安全**：
 *    不得与任何线程的日志调用并发执行，必须在调用方保证串行的前提下使用。
 *  
 *  生命周期：
 *  - 本对象由 `LoggerManager` 持有，调用方不构造、不删除。`destroy_logger` 之后该槽位对象仍留在池内
 *    并被清空：残留在该指针上的日志调用不产生输出，四个配置方法返回 false（实现只保证不崩溃）；
 *    但槽位可能被后续 `create_logger` 复用，届时同一指针指向新的日志器——因此销毁之后继续使用该
 *    指针仍是违约用法，不得依赖上述行为。
 *  - `shutdown` 之后所有 `Logger*` 整体失效（槽位对象的生命周期已结束）：此后经旧指针调用，**包括
 *    日志方法与配置方法**，属未定义行为，本插件不承诺安全，也不承诺不崩溃。
 *  
 *  运行期失败语义（与一次性配置失败不同）：
 *  - 到达 sink 的消息在格式化或写出时失败（例如磁盘满、句柄失效、格式串运行期不匹配）：不向调用方
 *    抛异常，该条消息被丢弃，写出入口继续返回 void，调用方**没有逐条失败返回值**可观测。失败由底层
 *    日志库的错误处理器向 stderr 报告，且限频为约每秒最多 1 条（带累计计数）。
 *  - 该"丢弃 + stderr 限频报告"来自底层日志库的行为，本插件未单独测量其开销与覆盖率，写为未测量。
 *  - 与 `fatal` 对照：`fatal` 的强语义是**终止进程**（`std::abort()`），不是"日志一定落盘"——
 *    flush 失败同样被吞掉，随后仍然终止；其余级别失败则只丢该条消息并继续运行。
 *  - 配置方法（`enable_sink` / `disable_sink` / `set_sink_level` / `set_sink_path`）的 `bool` 返回值是
 *    唯一的失败观测渠道：忽略返回值即放弃对目标是否真正可用的观测，此时"消息没出现在该目标"与
 *    "目标未挂载/配置被拒绝"不可区分。
 *  
 *  成本边界：
 *  - 消息被级别裁剪时：一次阈值读取 + 一次比较，不做格式化、不分配、不阻塞。
 *  - 消息未被裁剪时：每个放行的目标各自做一次 fmt 格式化（可能分配缓冲）后同步写出
 *    （可能阻塞在 IO）。
 *  - 具体的吞吐与延迟读数与评级见 `README.md` 的验证状态节与正式报告；本注释不转抄数字，
 *    也不给出量级估计。
 */

namespace ID::Log
{
    class LoggerManager;
    struct LoggerEntry;

    class Logger
    {
    public:
        /**
         *  @brief 记录 Trace 级日志
         *  @param format fmt 风格格式串；必须能通过编译期格式检查
         *  @param args 格式参数
         *  @note 可多线程并发调用
         */
        template <typename... Args>
        void trace(fmt::format_string<Args...> format, Args&&... args)
        {
            write(spdlog::level::trace, format, std::forward<Args>(args)...);
        }

        /**
         *  @brief 记录 Debug 级日志
         *  @note 可多线程并发调用
         */
        template <typename... Args>
        void debug(fmt::format_string<Args...> format, Args&&... args)
        {
            write(spdlog::level::debug, format, std::forward<Args>(args)...);
        }

        /**
         *  @brief 记录 Info 级日志
         *  @note 可多线程并发调用
         */
        template <typename... Args>
        void info(fmt::format_string<Args...> format, Args&&... args)
        {
            write(spdlog::level::info, format, std::forward<Args>(args)...);
        }

        /**
         *  @brief 记录 Warn 级日志
         *  @note 可多线程并发调用
         */
        template <typename... Args>
        void warn(fmt::format_string<Args...> format, Args&&... args)
        {
            write(spdlog::level::warn, format, std::forward<Args>(args)...);
        }

        /**
         *  @brief 记录 Error 级日志
         *  @note 可多线程并发调用；该级别的消息被放行时触发一次 flush
         *        （若所有已启用目标的阈值都高于 Error，消息在格式化前就被丢弃，也就不会触发）
         */
        template <typename... Args>
        void error(fmt::format_string<Args...> format, Args&&... args)
        {
            write(spdlog::level::err, format, std::forward<Args>(args)...);
        }

        /**
         *  @brief 记录 Fatal 级日志（critical），flush 后终止进程
         *  @note 不受任何级别配置影响：本插件不提供高于 critical 的阈值，因此该消息必然被放行；
         *        只有在没有任何已启用目标时才没有输出，但终止行为不变
         *  @note 为放行该消息会把底层阈值临时提升为 critical 且不恢复（进程即将终止）
         *  @note flush 失败不会抛出到调用方：异常在内部吞掉，随后仍然 `std::abort()`
         *  @note 调用后进程终止，不返回
         */
        template <typename... Args>
        void fatal(fmt::format_string<Args...> format, Args&&... args)
        {
            if (m_backend != nullptr)
            {
                try
                {
                    // 进程即将终止：强制放开后端阈值，避免任何遗留阈值吞掉这条消息
                    m_backend->set_level(spdlog::level::critical);
                    m_backend->log(spdlog::level::critical, format, std::forward<Args>(args)...);
                    m_backend->flush();
                }
                catch (...)
                {
                    // 落盘失败也必须终止进程；异常不跨出本对象
                }
            }

            std::abort();
        }

        /**
         *  @brief 启用一个输出目标
         *  @param type 目标类型，见 `SinkType`
         *  @return 目标已挂载到后端时返回 true；日志器已销毁、`type` 不是合法的 `SinkType` 取值、
         *          目标没有可用配置（`File` 尚未成功设置路径）或挂载失败时返回 false
         *  @note 非线程安全：不得与任何线程的日志调用并发
         *  @note `Console` 在创建日志器时已启用，重复调用为幂等
         *  @note 已创建过的目标连同其阈值一起复用；`File` 只有在 `set_sink_path` 成功过之后才可能启用
         *  @note 失败时不改变现有配置：原本启用的目标继续工作
         */
        ID_LOG_API bool enable_sink(SinkType type);

        /**
         *  @brief 停用一个输出目标（已产生的输出先 flush）
         *  @param type 目标类型，见 `SinkType`
         *  @return 日志器仍存活且 `type` 合法时返回 true（目标本来未挂载也算成功）；日志器已销毁，
         *          或 `type` 不是合法的 `SinkType` 取值时返回 false，且不改动任何已挂载目标
         *  @note 非线程安全
         *  @note 摘除后目标对象与其阈值保留；文件句柄保留到日志器销毁或切换到其它路径，
         *        再次 `enable_sink` 复用同一对象
         *  @note 两个目标都停用时后端阈值置为 off：没有任何输出，且消息在格式化之前就被挡掉
         */
        ID_LOG_API bool disable_sink(SinkType type);

        /**
         *  @brief 设置一个输出目标的级别裁剪阈值
         *  @param type 目标类型，见 `SinkType`
         *  @param level 低于该级别的消息不会输出到该目标；合法范围是 `Level::Trace` 到 `Level::Fatal`
         *  @return 日志器仍存活且 `type`、`level` 都合法时返回 true；日志器已销毁、`type` 不是合法的
         *          `SinkType` 取值，或 `level` 不是合法的 `Level` 取值（强制转换后超出 `Fatal`）时
         *          返回 false（该路径不改动任何阈值）
         *  @note 非线程安全
         *  @note 非法 `level` 与非法 `type` 同策略：以 false 拒绝且不改动任何阈值，不会静默退化成
         *        某个默认阈值（越界值若被放行会落到映射的默认分支，等价于全量放行）
         *  @note 对尚未创建或已停用的目标同样记录，该目标启用时生效
         */
        ID_LOG_API bool set_sink_level(SinkType type, Level level);

        /**
         *  @brief 设置目标路径并启用该目标
         *  @param type 目标类型；只有 `SinkType::File` 合法，`Console` 返回 false
         *  @param path 文件路径，UTF-8 窄字符串；Windows 下受系统 ACP 影响（已知限制）
         *  @return 成功 true；`type` 不是合法的 `SinkType` 取值或不是 `File`、路径为空、文件无法打开、
         *          内存不足或日志器已销毁返回 false
         *  @note 非线程安全
         *  @note 失败时不改变已有配置：原本可用的文件目标继续可用（新目标挂载成功后才摘除旧目标）
         *  @note 同一路径重复调用为幂等（该目标被停用时会重新接入）；传入不同路径时先创建并挂载
         *        新目标，成功后才 flush 并关闭旧目标
         *  @note 追加写入，不截断已有内容；父目录不存在时由底层自动创建
         */
        ID_LOG_API bool set_sink_path(SinkType type, const char* path);

    private:
        friend class LoggerManager;
        friend struct LoggerEntry;

        explicit Logger(std::shared_ptr<spdlog::logger> backend);

        // 由 LoggerManager / LoggerEntry 使用：句柄只能由 Factory（本类）构造
        static ::ID::Common::Handle<std::uint32_t, Logger> make_handle(std::uint32_t id) noexcept;

        template <typename... Args>
        void write(spdlog::level::level_enum level, fmt::format_string<Args...> format, Args&&... args)
        {
            if (m_backend == nullptr) [[unlikely]]
            {
                return;     // 条目已销毁：契约上句柄已失效，这里只保证不崩溃
            }

            m_backend->log(level, format, std::forward<Args>(args)...);
        }

        bool is_sink_attached(const std::shared_ptr<spdlog::sinks::sink>& sink) const noexcept;

        // 冲刷后从后端摘除目标；不会释放目标对象本身，便于保留其阈值再次启用
        void detach_sink(const std::shared_ptr<spdlog::sinks::sink>& sink) noexcept;

        // 把后端 logger 阈值同步为“已启用目标阈值的最小值”，使完全被裁剪的消息不进入格式化
        void sync_backend_level() noexcept;

        // 创建（首次）并接入终端目标；异常被吞掉，失败返回 false 且不改变已挂载的其它目标
        bool attach_console_sink();

        // 接入已创建的文件目标；尚无可用路径（目标为空）时返回 false
        bool attach_file_sink();

        // 记录并应用单目标阈值；目标尚未创建时只记录，创建时生效
        void apply_console_level(Level level) noexcept;
        void apply_file_level(Level level) noexcept;

        std::shared_ptr<spdlog::logger>         m_backend;                      // 底层 logger（含名称与 sink）
        std::shared_ptr<spdlog::sinks::sink>    m_console_sink;                 // 终端目标，未创建时为空
        std::shared_ptr<spdlog::sinks::sink>    m_file_sink;                    // 文件目标，未创建时为空
        std::string                             m_file_path;                    // 文件目标当前路径
        Level                                   m_console_level = Level::Trace; // 终端阈值
        Level                                   m_file_level    = Level::Trace; // 文件阈值
    };
} // namespace ID::Log
