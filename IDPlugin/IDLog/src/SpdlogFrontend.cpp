// IDPlugin/IDLog 里**唯一**包含 spdlog 头的编译单元。
// 下面三个宏必须在任何 include 之前定义，且在本 TU 内保持一致：
//   SPDLOG_HEADER_ONLY   header-only 接入：不建第三方库目标、不定义 SPDLOG_COMPILED_LIB
//   SPDLOG_NO_EXCEPTIONS 无异常：spdlog 内部抛错点表现为 printf + abort
//   FMT_EXCEPTIONS 0     内置 fmt 同样关闭异常，与编译器的 -fno-exceptions 对齐
#define SPDLOG_HEADER_ONLY
#define SPDLOG_NO_EXCEPTIONS
#define FMT_EXCEPTIONS 0

#include "SpdlogFrontend.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "spdlog/async.h"
#include "spdlog/details/thread_pool.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

/**
 *  spdlog 前端的实现（全工程唯一包含 spdlog 的 TU）
 *  1. sink 组合：console 用 `stderr_color_sink_mt`（pattern 不含颜色标记，因此实际不着色）；
 *     文件用 `basic_file_sink_mt`，`max_file_bytes > 0 && max_files > 0` 时换成 `rotating_file_sink_mt`；
 *  2. 日志对象：主 logger（`async_logger` 或 `logger`）+ Fatal 专用同步 logger，两者共享同一组 sink；
 *     **不调用 `spdlog::register_logger`**，也不调用 `spdlog::init_thread_pool` / `spdlog::shutdown`：
 *     线程池由 `Impl` 自己持有 `shared_ptr`，所有权与生命周期只跟随所属后端实例，
 *     因此重建后端不会碰任何 spdlog 全局状态；
 *  3. 级别过滤的唯一真源是 `g_active_level` 原子量，spdlog 侧 `set_level(trace)`；
 *  4. 消息在 `write` 内拼成 `[category] message` 后以 `spdlog::string_view_t` 提交，
 *     插件内不再走 fmt 解析；`Fatal` 同步输出 + flush + `std::abort()`，不进异步队列；
 *  5. 没有任何依赖静态析构的释放路径：资源只由显式 `shutdown()` 与后端交换释放。
 */

namespace ID::Log
{
    // 唯一过滤真源：常量初始化，零运行期全局构造
    constinit std::atomic<uint32_t> g_active_level{ static_cast<uint32_t>(Level::Info) };
    constinit std::atomic<uint32_t> g_last_config_error{ static_cast<uint32_t>(LogConfigError::None) };
    constinit std::atomic<uint64_t> g_dropped_before_ready{ 0 };
    constinit std::atomic<uint64_t> g_dropped_after_shutdown{ 0 };
    constinit std::atomic<bool>     g_ever_ready{ false };

    namespace
    {
        // 行的固定形态：日期 + 时间 + 级别 + 线程 id + 消息。category 已拼在消息里，因此不需要 per-category logger；
        // 不含 %^/%$，所以即使 sink 是 wincolor 变体也不会着色
        constexpr char kPattern[] = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v";

        /**
         *  @brief 把 `Level` 数值映射成 spdlog 档位（只用于标签与 Fatal 分派，不承担过滤）
         *  @param level 级别数值，调用方已收敛到 0..Fatal
         *  @return spdlog 档位；越界值按 Error 处理
         *  @note `Level::Fatal` 映射到 `critical`：Fatal 在输出里显示为 `critical`，这是 spdlog 的档位名。
         */
        spdlog::level::level_enum to_spdlog_level(uint32_t level) noexcept
        {
            switch (level)
            {
                case static_cast<uint32_t>(Level::Trace): return spdlog::level::trace;
                case static_cast<uint32_t>(Level::Debug): return spdlog::level::debug;
                case static_cast<uint32_t>(Level::Info):  return spdlog::level::info;
                case static_cast<uint32_t>(Level::Warn):  return spdlog::level::warn;
                case static_cast<uint32_t>(Level::Error): return spdlog::level::err;
                case static_cast<uint32_t>(Level::Fatal): return spdlog::level::critical;
                default:                                  return spdlog::level::err;
            }
        }

        /**
         *  @brief 把 src 的前 src_len 字节复制进 dst 的 [offset, capacity) 区间
         *  @param dst 目标缓冲；offset 必须 <= capacity
         *  @param capacity 目标缓冲总字节数
         *  @param offset 本次写入的起始偏移
         *  @param src 源指针；为 nullptr 时视为空内容
         *  @param src_len 源字节数
         *  @return 实际写入的字节数（可能被 capacity 截断）
         *  @note 只做 memcpy：不调用 strlen、不分配、不做字符处理，因此 category/message 都可以不含 NUL。
         */
        uint32_t append_capped(char* dst, uint32_t capacity, uint32_t offset, const char* src, uint32_t src_len) noexcept
        {
            if (src == nullptr || src_len == 0 || offset >= capacity)
            {
                return 0;
            }

            uint32_t count = capacity - offset;
            if (count > src_len)
            {
                count = src_len;
            }

            std::memcpy(dst + offset, src, count);
            return count;
        }

        /**
         *  @brief 探测文件是否可写
         *  @param path 目标路径（NUL 结尾）
         *  @return 能以追加模式打开时为 true
         *  @note 无异常模式下文件 sink 的构造失败会直接终止进程，因此建后端前必须先探测一次。
         */
        bool file_is_writable(const char* path) noexcept
        {
            std::FILE* const probe = std::fopen(path, "ab");
            if (probe == nullptr)
            {
                return false;
            }
            std::fclose(probe);
            return true;
        }

        /**
         *  @brief 归一化路径并按 ABI 握手 + 级别校验配置
         *  @param config 调用方传入的配置
         *  @param out 归一化结果：`file_path` 末字节强制 NUL，握手字段写成当前构建的规范值
         *  @return 合法时为 `LogConfigError::None`
         */
        LogConfigError validate_envelope(const LogConfig& config, LogConfig& out) noexcept
        {
            // 调用方只保证 file_path 是一个 kMaxFilePathBytes 字节的数组，不保证以 NUL 结尾：
            // 未结尾的满长数组会让 std::fopen 与 std::string 一次性越过缓冲边界。这里不信任调用方，
            // 先把容量范围内最后一个字节强制写成 '\0'（等价于按容量截断），之后所有路径读取都走这份副本
            out = config;
            out.file_path[kMaxFilePathBytes - 1] = '\0';

            // ABI 握手：0 表示"按当前构建解释"（兼容源码里的 {} 初始化），非 0 必须与本编译单元一致
            if (config.struct_size != 0 && config.struct_size != static_cast<uint32_t>(sizeof(LogConfig)))
            {
                return LogConfigError::InvalidEnvelope;
            }
            if (config.config_revision != 0 && config.config_revision != kLogApiRevision)
            {
                return LogConfigError::InvalidEnvelope;
            }
            if (out.min_level > static_cast<uint32_t>(Level::Fatal))
            {
                return LogConfigError::InvalidLevel;   // 越界级别：不能把非法整数当成"全部打开"
            }

            out.struct_size     = static_cast<uint32_t>(sizeof(LogConfig));
            out.config_revision = kLogApiRevision;
            return LogConfigError::None;
        }

        /**
         *  @brief 校验"要新建后端"的配置：队列容量、文件路径与 sink 组合
         *  @param config 已归一化的配置
         *  @return 合法时为 `LogConfigError::None`
         *  @note 会做一次 fopen 探测（可能创建文件），因此只在确实要建后端时调用，
         *        不能放在"拓扑差异必被拒绝"的 configure 路径上。
         */
        LogConfigError validate_backend(const LogConfig& config) noexcept
        {
            // queue_size 只在异步形态下有意义：同步形态不进队列，因此不参与校验
            if (config.async && (config.queue_size == 0 || (config.queue_size & (config.queue_size - 1)) != 0))
            {
                return LogConfigError::InvalidQueueSize;   // 先自行校验，不把 spdlog 的失败推到运行期
            }
            if (config.enable_file && config.file_path[0] == '\0')
            {
                return LogConfigError::MissingFilePath;
            }
            if (config.enable_file && !file_is_writable(config.file_path))
            {
                return LogConfigError::FileNotWritable;
            }
            if (!config.enable_console && !config.enable_file)
            {
                return LogConfigError::NoSink;   // 没有任何 sink：日志与 Fatal 的最后一行都会无声丢失
            }
            return LogConfigError::None;
        }

        /**
         *  @brief 比较两份配置的拓扑字段（决定是否需要重建后端）
         *  @return 拓扑一致（只差 `min_level` / `flush_interval_ms`）时为 true
         */
        bool topology_equal(const LogConfig& left, const LogConfig& right) noexcept
        {
            return left.enable_console      == right.enable_console
                && left.enable_file         == right.enable_file
                && left.async               == right.async
                && left.overrun_drop_oldest == right.overrun_drop_oldest
                && left.queue_size          == right.queue_size
                && left.flush_on_warn       == right.flush_on_warn
                && left.max_file_bytes      == right.max_file_bytes
                && left.max_files           == right.max_files
                && std::strncmp(left.file_path, right.file_path, kMaxFilePathBytes) == 0;
        }

        /**
         *  @brief 把 category 与 message 拼成 `[category] message`，超长时按 UTF-8 边界截断并补 "..."
         *  @param line 目标缓冲，容量必须是 `kLineCapacity`
         *  @param category 分类标签；允许 nullptr
         *  @param category_len 分类字节数
         *  @param message 消息；允许 nullptr
         *  @param message_len 消息字节数
         *  @return 有效字节数，不超过 `kLineCapacity`
         *  @note 返回值只覆盖**真实写入**的字节，绝不按名义长度计算：`nullptr` 段与零长段
         *        不写任何字节，也不计入长度。否则调用方拿返回长度构造的 `string_view` 会涵盖
         *        缓冲区尾部未初始化的栈内容（`write(lv, nullptr, 2000, "x", 1)` 就是这种输入）。
         *        只有确实装不下某一段（请求字节数 > 剩余容量）时才在末尾补 "..."，
         *        且 "..." 只覆盖已经写入的内容，不写进未写入区域。
         */
        uint32_t compose_line(char* line, const char* category, uint32_t category_len,
                              const char* message, uint32_t message_len) noexcept
        {
            uint32_t used    = 0;
            bool     clipped = false;

            // 逐段追加：真实长度只取 append_capped 的实际写入字节数；请求长度与它的差只用于判定截断
            const auto append = [&](const char* src, uint32_t len) noexcept
            {
                if (src == nullptr)
                {
                    return;   // 该段没有内容：不写、不占长度、也不算截断
                }
                if (len > kLineCapacity - used)
                {
                    clipped = true;   // 剩余容量装不下这一段（len == 0 时恒为假，因此零长段不算截断）
                }
                used += append_capped(line, kLineCapacity, used, src, len);
            };

            append("[", 1);
            append(category, category_len);
            append("] ", 2);
            append(message, message_len);

            // clipped 为真时 append_capped 一定已把缓冲填满，因此按满容量收敛 UTF-8 边界并补 "..."
            return clipped ? detail::mark_truncated(line, kLineCapacity, kLineCapacity) : used;
        }

        /**
         *  @brief 后端不可用时的 Fatal 路径：拼一行后交给 `BootstrapLog`，然后终止进程
         *  @param category 分类标签；允许 nullptr
         *  @param category_len 分类字节数
         *  @param message 已格式化消息；允许 nullptr
         *  @param message_len 消息字节数
         *  @note 本函数不返回。`BootstrapLog::report(Fatal, …)` 自己会 abort；这里再补一次 `std::abort()`
         *        是为了覆盖 sink 返回后的所有路径，保证 Fatal 的"必终止"语义不依赖内核实现细节。
         */
        [[noreturn]] ID_NOINLINE void fatal_without_backend(const char* category, uint32_t category_len,
                                                            const char* message, uint32_t message_len) noexcept
        {
            char line[kLineCapacity];
            const uint32_t used = compose_line(line, category, category_len, message, message_len);

            BootstrapLog::instance().report(Level::Fatal, "ID.Log", std::string_view(line, used));
            std::abort();
        }
    } // namespace

    /**
     *  pimpl 实体：spdlog 的 logger、sink 与线程池句柄全部关在这里，本头之外看不到任何 spdlog 类型
     *  @note 析构只做成员释放，不主动 flush；资源的排空与释放顺序由 `destroy_impl` 编排。
     */
    struct Impl
    {
        std::shared_ptr<spdlog::details::thread_pool>   pool;           // 仅 async：本实例独占，不经 spdlog registry
        std::shared_ptr<spdlog::logger>                 main_logger;    // 主 logger：async_logger 或 logger
        std::shared_ptr<spdlog::logger>                 fatal_logger;   // Fatal 专用同步 logger，共享同一组 sink
        std::vector<spdlog::sink_ptr>                   sinks;
        LogConfig                                       config{};
        std::thread                                     flusher;
        std::mutex                                      flusher_mutex;
        std::condition_variable                         flusher_cv;
        std::atomic<bool>                               flusher_stop{ false };

        /**
         *  @brief 排空当前所有 sink
         *  @note 主 logger 是异步时只会入队一个 flush 屏障；随后同步 logger 的 flush 才真正把
         *        已经写进 sink 的内容刷到 OS。两者合起来给 `flush()` 一个可见的即时效果。
         */
        void flush_all() noexcept
        {
            if (main_logger != nullptr)
            {
                main_logger->flush();
            }
            if (fatal_logger != nullptr)
            {
                fatal_logger->flush();
            }
        }

        /**
         *  @brief 停掉周期性 flush 线程（幂等）
         */
        void stop_flusher() noexcept
        {
            if (!flusher.joinable())
            {
                return;
            }
            {
                const std::lock_guard<std::mutex> lock(flusher_mutex);
                flusher_stop.store(true, std::memory_order_relaxed);
            }
            flusher_cv.notify_all();
            flusher.join();
        }

        /**
         *  @brief 按 interval_ms 重启周期性 flush 线程；0 表示关闭
         */
        void start_flusher(uint32_t interval_ms) noexcept
        {
            stop_flusher();
            if (interval_ms == 0)
            {
                return;
            }
            flusher_stop.store(false, std::memory_order_relaxed);
            flusher = std::thread([this, interval_ms]() noexcept { flusher_loop(interval_ms); });
        }

        /**
         *  @brief 周期性 flush 循环
         *  @note 用条件变量等待而不是 sleep，`stop_flusher()` 可以立刻唤醒退出；
         *        flush 期间不持锁，避免与 `stop_flusher()` 互相阻塞。
         */
        void flusher_loop(uint32_t interval_ms) noexcept
        {
            const auto period = std::chrono::milliseconds(interval_ms);
            std::unique_lock<std::mutex> lock(flusher_mutex);
            while (!flusher_stop.load(std::memory_order_relaxed))
            {
                const bool stopped = flusher_cv.wait_for(lock, period, [this]() noexcept
                {
                    return flusher_stop.load(std::memory_order_relaxed);
                });
                if (stopped)
                {
                    break;
                }
                lock.unlock();
                flush_all();
                lock.lock();
            }
        }
    };

    namespace
    {
        /**
         *  @brief 应用运行期可调项（`min_level` / `flush_interval_ms`）
         *  @note 调用方必须先确认拓扑一致，否则 sink / 队列形态与配置会不一致。
         */
        void apply_runtime_fields(Impl& impl, const LogConfig& config) noexcept
        {
            g_active_level.store(config.min_level, std::memory_order_relaxed);
            impl.config.min_level = config.min_level;

            if (config.flush_interval_ms != impl.config.flush_interval_ms)
            {
                impl.config.flush_interval_ms = config.flush_interval_ms;
                impl.start_flusher(config.flush_interval_ms);
            }
        }

        /**
         *  @brief 按已校验的配置搭建一个全新后端
         *  @param config 归一化并校验过的配置
         *  @return 新后端；分配或 sink 组合失败时返回 nullptr（不触碰任何已有后端）
         *  @note async 形态先建本实例独占的线程池，再把 `shared_ptr` 传给 `async_logger`（内部转 `weak_ptr`）：
         *        线程池的生命周期由 `Impl::pool` 独占，与 spdlog 全局状态无关。
         */
        std::unique_ptr<Impl> build_impl(const LogConfig& config) noexcept
        {
            std::unique_ptr<Impl> impl = std::make_unique<Impl>();
            impl->config = config;

            if (config.enable_console)
            {
                impl->sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
            }
            if (config.enable_file)
            {
                const std::string path{ config.file_path };
                if (config.max_file_bytes > 0 && config.max_files > 0)
                {
                    impl->sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        path, static_cast<size_t>(config.max_file_bytes), static_cast<size_t>(config.max_files)));
                }
                else
                {
                    impl->sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path));
                }
            }
            if (impl->sinks.empty())
            {
                return nullptr;
            }

            if (config.async)
            {
                impl->pool = std::make_shared<spdlog::details::thread_pool>(
                    static_cast<size_t>(config.queue_size), 1);
                const spdlog::async_overflow_policy policy = config.overrun_drop_oldest
                                                                 ? spdlog::async_overflow_policy::overrun_oldest
                                                                 : spdlog::async_overflow_policy::block;
                impl->main_logger = std::make_shared<spdlog::async_logger>(
                    "ID.Log", impl->sinks.begin(), impl->sinks.end(), impl->pool, policy);
            }
            else
            {
                impl->main_logger = std::make_shared<spdlog::logger>(
                    "ID.Log", impl->sinks.begin(), impl->sinks.end());
            }
            impl->fatal_logger = std::make_shared<spdlog::logger>(
                "ID.Log.Fatal", impl->sinks.begin(), impl->sinks.end());

            // spdlog 侧级别固定 trace：关闭哪一档只由 g_active_level 决定，改级别不触碰 spdlog
            impl->main_logger->set_level(spdlog::level::trace);
            impl->main_logger->flush_on(config.flush_on_warn ? spdlog::level::warn : spdlog::level::off);
            impl->fatal_logger->set_level(spdlog::level::trace);
            impl->fatal_logger->flush_on(spdlog::level::off);

            // 两个 logger 共享同一组 sink 对象，pattern 只需设一次（set_pattern 会按 sink 克隆 formatter）
            impl->main_logger->set_pattern(kPattern);
            return impl;
        }

        /**
         *  @brief 销毁一个后端：先停周期 flush，再放 logger，最后析构线程池
         *  @param impl 目标后端；允许 nullptr（幂等）
         *  @note 顺序不可交换：
         *        1. `main_logger` 里的异步消息各自持有 worker 的 `shared_ptr`，因此 logger 对象在最后一条
         *           消息被处理完之前不会销毁；
         *        2. `pool.reset()` 触发 `thread_pool` 析构：投递 terminate（block 策略）后 join，
         *           已入队且未被 drop 的消息按 FIFO 处理完才返回；
         *        3. logger 销毁时才释放它对 sink 的引用，因此 sink 必须在 `pool.reset()` 之后再清。
         */
        void destroy_impl(Impl* impl) noexcept
        {
            if (impl == nullptr)
            {
                return;
            }
            impl->stop_flusher();
            impl->flush_all();

            impl->main_logger.reset();
            impl->fatal_logger.reset();
            impl->pool.reset();
            impl->sinks.clear();

            delete impl;
        }

        /**
         *  @brief 记录最近一次配置失败原因（relaxed，仅诊断）
         */
        void record_config_error(LogConfigError error) noexcept
        {
            g_last_config_error.store(static_cast<uint32_t>(error), std::memory_order_relaxed);
        }
    } // namespace

    bool SpdlogFrontend::init(const LogConfig& config) noexcept
    {
        if (m_impl.load(std::memory_order_acquire) != nullptr)
        {
            // 已初始化：重复 init 不是可恢复的配置变更，必须先 shutdown
            record_config_error(LogConfigError::AlreadyReady);
            return false;
        }

        LogConfig normalized{};
        LogConfigError error = validate_envelope(config, normalized);
        if (error == LogConfigError::None)
        {
            error = validate_backend(normalized);
        }
        if (error != LogConfigError::None)
        {
            record_config_error(error);
            return false;
        }

        std::unique_ptr<Impl> impl = build_impl(normalized);
        if (impl == nullptr)
        {
            record_config_error(LogConfigError::NoSink);
            return false;
        }

        Impl* const raw = impl.release();
        raw->start_flusher(normalized.flush_interval_ms);

        g_active_level.store(normalized.min_level, std::memory_order_relaxed);
        m_impl.store(raw, std::memory_order_release);
        g_ever_ready.store(true, std::memory_order_relaxed);
        record_config_error(LogConfigError::None);
        return true;
    }

    void SpdlogFrontend::shutdown() noexcept
    {
        // 先断开入口：之后再有 write 进来只会记一条丢弃计数，不会碰到正在销毁的对象
        Impl* const impl = m_impl.exchange(nullptr, std::memory_order_acq_rel);
        destroy_impl(impl);
    }

    bool SpdlogFrontend::configure(const LogConfig& config) noexcept
    {
        Impl* const impl = m_impl.load(std::memory_order_acquire);
        if (impl == nullptr)
        {
            record_config_error(LogConfigError::NotReady);
            return false;
        }

        // configure 只做握手与级别校验：拓扑差异必须在探测文件之前被判定为"需 reconfigure"，
        // 否则一次注定被拒的请求会顺手创建/探测目标文件
        LogConfig normalized{};
        const LogConfigError error = validate_envelope(config, normalized);
        if (error != LogConfigError::None)
        {
            record_config_error(error);
            return false;
        }

        if (!topology_equal(normalized, impl->config))
        {
            // 换 sink / 改队列容量 / 切换同步异步都需要重建后端：整条请求被拒，
            // 同一请求里的 min_level 也不得被顺手应用
            record_config_error(LogConfigError::TopologyRequiresReconfigure);
            return false;
        }

        apply_runtime_fields(*impl, normalized);
        record_config_error(LogConfigError::None);
        return true;
    }

    bool SpdlogFrontend::reconfigure(const LogConfig& config) noexcept
    {
        Impl* const current = m_impl.load(std::memory_order_acquire);
        if (current == nullptr)
        {
            record_config_error(LogConfigError::NotReady);
            return false;
        }

        LogConfig normalized{};
        LogConfigError error = validate_envelope(config, normalized);
        if (error == LogConfigError::None)
        {
            error = validate_backend(normalized);
        }
        if (error != LogConfigError::None)
        {
            record_config_error(error);
            return false;   // 校验失败：旧后端与旧状态完全不变
        }

        // 只有运行期可调项变化：等同 configure，不重建后端（也就不必排空旧队列）
        if (topology_equal(normalized, current->config))
        {
            apply_runtime_fields(*current, normalized);
            record_config_error(LogConfigError::None);
            return true;
        }

        // 拓扑变化：先建出全新后端，成功后才交换；这一步失败时旧后端仍然可用
        std::unique_ptr<Impl> fresh = build_impl(normalized);
        if (fresh == nullptr)
        {
            record_config_error(LogConfigError::NoSink);
            return false;
        }
        fresh->start_flusher(normalized.flush_interval_ms);

        Impl* const previous = m_impl.exchange(fresh.release(), std::memory_order_acq_rel);
        g_active_level.store(normalized.min_level, std::memory_order_relaxed);
        g_ever_ready.store(true, std::memory_order_relaxed);

        // 旧后端已不再被 write/flush 引用：销毁时按 FIFO 排空它未被 drop 的消息
        destroy_impl(previous);
        record_config_error(LogConfigError::None);
        return true;
    }

    void SpdlogFrontend::get_stats(LogStats* stats) noexcept
    {
        if (stats == nullptr)
        {
            return;
        }
        *stats = LogStats{};

        Impl* const impl = m_impl.load(std::memory_order_acquire);
        if (impl == nullptr)
        {
            stats->ready             = false;
            stats->last_config_error = g_last_config_error.load(std::memory_order_relaxed);
            return;
        }

        if (impl->pool != nullptr)
        {
            stats->overrun_dropped = static_cast<uint64_t>(impl->pool->overrun_counter());
            stats->discarded       = static_cast<uint64_t>(impl->pool->discard_counter());
        }
        stats->dropped_before_ready   = g_dropped_before_ready.load(std::memory_order_relaxed);
        stats->dropped_after_shutdown = g_dropped_after_shutdown.load(std::memory_order_relaxed);
        stats->queue_capacity         = impl->config.async ? impl->config.queue_size : 0;
        stats->active_level           = g_active_level.load(std::memory_order_relaxed);
        stats->last_config_error      = g_last_config_error.load(std::memory_order_relaxed);
        stats->ready                  = true;
        stats->async                  = impl->config.async;
        stats->console                = impl->config.enable_console;
        stats->file                   = impl->config.enable_file;
    }

    void SpdlogFrontend::write(uint32_t level, const char* category, uint32_t category_len,
                               const char* message, uint32_t message_len) noexcept
    {
        // 越界级别收敛为 Error：保护 LogApiTable::write 与内核 bridge 两个入口，
        // 非法整数不得把进程推进 Fatal 的 abort 分支（这是唯一的收敛点）
        if (level > static_cast<uint32_t>(Level::Fatal))
        {
            level = static_cast<uint32_t>(Level::Error);
        }

        Impl* const impl = m_impl.load(std::memory_order_acquire);
        if (impl == nullptr || impl->main_logger == nullptr)
        {
            if (level == static_cast<uint32_t>(Level::Fatal))
            {
                fatal_without_backend(category, category_len, message, message_len);   // 不返回
            }

            // 没有后端：按"是否曾经 Ready 过"把丢弃记到前/后两个计数器上（relaxed，仅诊断）
            if (g_ever_ready.load(std::memory_order_relaxed))
            {
                g_dropped_after_shutdown.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                g_dropped_before_ready.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }

        // 过滤的唯一真源，与 Logger::is_enabled 读的是同一个原子量；Fatal 不受它约束
        if (level != static_cast<uint32_t>(Level::Fatal)
            && level < g_active_level.load(std::memory_order_relaxed))
        {
            return;
        }

        char line[kLineCapacity];
        const uint32_t used = compose_line(line, category, category_len, message, message_len);

        const spdlog::string_view_t payload{ line, used };
        const spdlog::level::level_enum spd_level = to_spdlog_level(level);

        if (level == static_cast<uint32_t>(Level::Fatal))
        {
            // Fatal 不走异步队列：同步 logger 写完立刻 flush 再 abort，保证崩溃前最后一行一定落地
            impl->fatal_logger->log(spd_level, payload);
            impl->fatal_logger->flush();
            std::abort();
        }

        impl->main_logger->log(spd_level, payload);
    }

    void SpdlogFrontend::flush() noexcept
    {
        Impl* const impl = m_impl.load(std::memory_order_acquire);
        if (impl != nullptr)
        {
            impl->flush_all();
        }
    }
} // namespace ID::Log
