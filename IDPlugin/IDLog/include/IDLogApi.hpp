#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "Common/Core.hpp"
#include "Kernel/BootstrapLog.hpp"
#include "Kernel/CoreTypes.hpp"
#include "Kernel/Plugin.hpp"

// 逐插件导出头由组装生成器产出：STATIC 归并时宏为空，Windows SHARED 时只有本插件自己的
// build 宏（PRIVATE，不向下游传播）能打开 dllexport
#include "IDLogExport.generated.hpp"

/**
 *  ID.Log 的稳定 ABI 面（公开头）
 *  1. 本头只放常量、配置 POD、统计 POD 与函数表：**不包含 `<format>`、不包含任何 spdlog 头**。
 *     内核、其它插件与宿主只能经这里使用日志能力，spdlog 与 `<format>` 都不出现在它们的编译单元里；
 *  2. `LogApiTable` 由固定函数指针、一个 `const std::atomic<uint32_t>*` 与修订号组成，可平凡复制，
 *     因此可以安全地放进 `ServiceSlot::Log` 槽并跨 DLL 传递；表本体具有静态存储期，地址在进程内不变；
 *  3. 日志级别的唯一真源是 `Kernel/BootstrapLog.hpp` 的 `ID::Log::Level`，本插件不自建第二套枚举；
 *  4. 调用侧格式化在 `IDLogLogger.hpp`（唯一包含 `<format>` 的公开头）完成；本插件只做
 *     `[category] message` 拼接，不再解析格式串。
 */

// 编译期最低输出级别（数值与 ID::Log::Level 对齐）。唯一真源就是下一行：外部（`-D` 或任何构建定义）
// 一律不得覆盖——不同 TU 看到不同底线会让 inline 的 `Logger` 方法在不同模块里语义不一致（ODR 风险），
// 而且非法值还能把 `Fatal` 静默裁掉。需要按产品裁剪时改这里并重新构建。
#if defined(ID_LOG_MIN_LEVEL)
    #error "ID_LOG_MIN_LEVEL 由 IDLogApi.hpp 固定，不得由构建定义或 -D 覆盖"
#endif
#define ID_LOG_MIN_LEVEL 0

namespace ID::Log
{
    inline constexpr uint32_t kLogApiRevision      = 2;        // 本实现的接口修订，必须与 plugin.json 的 provides.api_revision 一致
    inline constexpr uint32_t kLogSlot             = 0;        // 运行期服务槽位 = ServiceSlot::Log
    inline constexpr uint32_t kMaxLoggerHandles    = 64;       // acquire_logger 的固定槽位数（冷路径，无扩容）
    inline constexpr uint32_t kStackFormatCapacity = 512;      // 调用侧格式化栈缓冲字节数
    inline constexpr uint32_t kLineCapacity        = 1024;     // write 内拼装 "[category] message" 的字节上限
    inline constexpr uint32_t kMaxFilePathBytes    = 260;      // LogConfig::file_path 容量（含末尾 NUL）

    // 槽位数值属于跨插件 ABI：0 是内核在 ServiceSlot 里声明的 Log 槽，不是业务自选槽
    static_assert(kLogSlot == 0, "ID.Log 的槽位必须是 ServiceSlot::Log 的数值 0");
    static_assert(kLogSlot == static_cast<uint32_t>(ServiceSlot::Log), "kLogSlot 必须与 ServiceSlot::Log 一致");

    // 完整定义在 IDLogLogger.hpp（那里才包含 <format>）；ABI 面只用到指针
    struct Logger;

    namespace detail
    {
        /**
         *  @brief 判定编译期最低级别是否落在合法范围
         *  @param level 级别数值
         *  @return 在 [`Level::Trace`, `Level::Fatal`] 内时为 true
         *  @note 判据写成函数形参而不是直接内联到 `static_assert`，是为了避免 GCC 把
         *        `枚举常量 >= 0` 折叠成单点值后报 `-Wtype-limits`。
         */
        constexpr bool min_level_in_range(int level) noexcept
        {
            return level >= static_cast<int>(Level::Trace) && level <= static_cast<int>(Level::Fatal);
        }

        /**
         *  @brief 把截断后的内容收敛到 UTF-8 码点边界，并写上限长标记 "..."
         *  @param buffer 目标缓冲；内容是格式化/拼接结果的前若干字节
         *  @param capacity 缓冲总字节数（本函数写入上限就是它，绝不越界）
         *  @param full_size 未截断时内容的完整字节数
         *  @return 有效内容的字节数：未截断时就是 `full_size`；截断时是 `容量 - 3` 起向前回退到码点边界后的长度加 3
         *  @note 截断时预留末尾 3 字节写 "..."，并最多向前回退 3 字节丢掉被切断的多字节序列，
         *        因此输出永远是合法 UTF-8 前缀 + "..."，不会留下半个码点。
         *        调用方必须保证 `buffer` 里前 `min(full_size, capacity)` 字节是真实内容。
         */
        inline uint32_t mark_truncated(char* buffer, uint32_t capacity, uint32_t full_size) noexcept
        {
            if (full_size < capacity)
            {
                return full_size;   // 内容完整落在缓冲里，不需要标记
            }
            if (capacity < 4)
            {
                return 0;           // 容量太小：既留不出 "..." 又要求不越界，只能放弃内容
            }

            uint32_t cut = capacity - 3;    // 末尾 3 字节留给 "..."
            for (uint32_t back = 1; back <= 3 && cut >= back; ++back)
            {
                const auto lead = static_cast<unsigned char>(buffer[cut - back]);
                if ((lead & 0xC0) == 0x80)
                {
                    continue;   // 延续字节：切点落在多字节序列中间，继续往前找序列首字节
                }

                uint32_t sequence_bytes = 1;
                if ((lead & 0xE0) == 0xC0)
                {
                    sequence_bytes = 2;
                }
                else if ((lead & 0xF0) == 0xE0)
                {
                    sequence_bytes = 3;
                }
                else if ((lead & 0xF8) == 0xF0)
                {
                    sequence_bytes = 4;
                }
                if (sequence_bytes > back)
                {
                    cut -= back;    // 整个不完整序列都丢掉，避免输出半个码点
                }
                break;
            }

            buffer[cut]     = '.';
            buffer[cut + 1] = '.';
            buffer[cut + 2] = '.';
            return cut + 3;
        }
    } // namespace detail

    static_assert(detail::min_level_in_range(ID_LOG_MIN_LEVEL),
                  "ID_LOG_MIN_LEVEL 必须在 [Level::Trace, Level::Fatal] 之内：非法值会把 Fatal 静默裁掉");

    /**
     *  @brief `configure` / `reconfigure` 的失败原因（`LogStats::last_config_error` 的取值集合）
     *  @note 纯 POD 枚举，跨 DLL 只传数值；`None = 0` 表示上一次配置调用成功。
     */
    enum class LogConfigError : uint32_t
    {
        None                          = 0,    // 上一次配置调用成功
        NotReady                      = 1,    // 插件未初始化的 Ready 区间之外
        AlreadyReady                  = 2,    // 重复 init
        InvalidEnvelope               = 3,    // struct_size / config_revision 与本构建不匹配
        InvalidLevel                  = 4,    // min_level 越界
        InvalidQueueSize              = 5,    // 异步形态下 queue_size 为 0 或不是 2 的幂
        MissingFilePath               = 6,    // 打开文件 sink 却没有路径
        FileNotWritable               = 7,    // 文件路径不可写（探测 fopen 失败）
        NoSink                        = 8,    // console 与 file 都没打开：日志无处可去
        TopologyRequiresReconfigure   = 9     // `configure` 收到拓扑字段差异，必须改用 `reconfigure`
    };

    /**
     *  @brief 运行期诊断读数（纯 POD，可平凡复制）
     *  @note `LogApiTable::get_stats` 的填充目标；计数只用于观测，不参与任何正确性判断。
     *        两个丢弃计数是进程级累计量（跨 start/shutdown 不归零），`overrun_dropped` / `discarded`
     *        是当前后端线程池的计数，后端销毁后即消失。
     */
    struct LogStats
    {
        uint64_t overrun_dropped        = 0;        // 队列满时被覆盖的最旧消息数（thread_pool overrun_counter）
        uint64_t discarded              = 0;        // 显式丢弃的新消息数（discard_new 策略；本实现不使用）
        uint64_t dropped_before_ready   = 0;        // 从未 Ready 过就进来的写入数（进程级累计）
        uint64_t dropped_after_shutdown = 0;        // 曾经 Ready 过、后端已销毁后进来的写入数（进程级累计）
        uint32_t queue_capacity         = 0;        // 异步队列容量；同步形态为 0
        uint32_t active_level           = 0;        // 当前过滤级别（与 active_level 原子量同源）
        uint32_t last_config_error      = 0;        // 最近一次 configure/reconfigure/init 的 LogConfigError 数值
        bool     ready                  = false;    // 后端是否处于可用状态
        bool     async                  = false;    // 当前是否异步形态
        bool     console                = false;    // 当前是否有 console sink
        bool     file                   = false;    // 当前是否有文件 sink
    };

    /**
     *  @brief 日志配置（纯 POD，按值初始化、按常量引用传入）
     *  @note Ready 区间的两种改法：
     *        - `LogApiTable::configure` 只接受 `min_level` 与 `flush_interval_ms`；
     *        - `LogApiTable::reconfigure` 接受全部字段（重建后端）。
     *        `queue_size` 只在 `async == true` 时校验（必须非 0 且是 2 的幂）；同步形态不进队列，不看它。
     */
    struct LogConfig
    {
        uint32_t    min_level           = static_cast<uint32_t>(Level::Info);    // 唯一过滤真源；spdlog 侧固定 trace
        uint32_t    flush_interval_ms   = 0;        // 0 = 不启用周期性 flush
        uint32_t    queue_size          = 8192;     // 异步队列容量；仅 async 形态校验，必须非 0 且是 2 的幂
        uint32_t    max_files           = 0;        // 轮转保留文件数（与 max_file_bytes 同时非 0 才启用轮转）
        uint64_t    max_file_bytes      = 0;        // 单个日志文件字节上限
        bool        enable_console      = true;     // stderr sink（pattern 不含颜色标记，因此不着色）
        bool        enable_file         = false;    // 文件 sink（需要 file_path）
        bool        async               = true;     // async_logger + 本实例独占的线程池
        bool        overrun_drop_oldest = true;     // 队列满丢最旧；不等待空位，但仍会取队列锁
        bool        flush_on_warn       = true;     // warn 及以上立即 flush
        char        file_path[kMaxFilePathBytes] = {};    // 必须以 NUL 结尾，插件会按容量强制截断（末字节被覆写为 '\0'）

        // ABI 握手：0 表示"按当前构建解释"，非 0 必须与本编译单元一致，否则配置被拒绝且状态不变
        uint32_t    struct_size         = sizeof(LogConfig);
        uint32_t    config_revision     = kLogApiRevision;
    };

    /**
     *  @brief 发布到 `ServiceSlot::Log` 的运行期函数表
     *  @note 表本体必须是静态存储期对象（`constexpr`/`constinit`），地址在进程内不变；
     *        读取侧是固定槽位下标 + 一次指针 load，不做状态、owner 或版本校验。
     *        表里的写入口（`write`）可以由多线程并发调用；其余入口是冷路径，
     *        并且要求宿主保证不与 `shutdown` 并发（见各自注记）。
     */
    struct LogApiTable
    {
        /**
         *  @brief 写一条日志（热路径：跨 DLL 一次间接调用）
         *  @param level 级别数值，对应 `ID::Log::Level`；越界值一律收敛为 `Error`，
         *               不允许非法整数把进程推进 Fatal 分支
         *  @param category 分类标签；允许为 nullptr，只在本调用内读取，不接管所有权
         *  @param category_len category 字节数；不要求 NUL 结尾，插件不调用 strlen
         *  @param message 已格式化好的消息；允许为 nullptr，不要求 NUL 结尾
         *  @param message_len message 字节数
         *  @note `Level::Fatal` 无条件终止：同步写 + flush 后 `std::abort()`，不进异步队列，
         *        也不受 `active_level` 过滤。后端不可用（未初始化 / 已 shutdown）时同样终止：
         *        消息转交 `BootstrapLog` 的 Fatal 后 abort。
         */
        void(*write)(uint32_t level, const char* category, uint32_t category_len,
                     const char* message, uint32_t message_len) noexcept = nullptr;

        /**
         *  @brief 冷路径：在 Ready 区间内改变运行期可调项
         *  @param config 目标配置；只允许 `min_level` 与 `flush_interval_ms` 变化
         *  @return 只改了这两项且取值合法时为 true。以下字段任一变化都返回 false（拓扑需重建，
         *          改用 `reconfigure`）：`enable_console`、`enable_file`、`async`、`queue_size`、
         *          `flush_on_warn`、`overrun_drop_oldest`、`max_file_bytes`、`max_files`、
         *          `file_path`（任何与当前取值不同的路径）
         *  @note 返回 false 时不改变任何运行期状态：被拒的请求里的 `min_level` 不会被顺手应用。
         *        越界 `min_level` 同样返回 false，不接受把非法整数当成"全部打开"。
         *        失败原因写进 `LogStats::last_config_error`。
         *        线程约束：宿主必须保证本调用不与 `write`/`flush`/`reconfigure`/`get_stats`/shutdown 并发。
         */
        bool(*configure)(const LogConfig& config) noexcept = nullptr;

        /**
         *  @brief 冷路径：排空 sink
         *  @note 同步模式直接 flush 所有 sink（返回时已交给 OS）。异步模式投递一个 flush 屏障，
         *        保证屏障之前入队的消息最终会被处理，但本调用**不等待**屏障完成，
         *        也不保证队列已排空或数据物理持久化（`fflush` 不是 `fsync`）。
         *        线程约束：宿主必须保证不与 shutdown 并发。
         */
        void(*flush)() noexcept = nullptr;

        /**
         *  @brief 冷路径：为一个 category 申请 `Logger` 槽位
         *  @param category 分类标签；允许为 nullptr（此时返回 nullptr），只保存指针，不复制内容
         *  @param category_len category 字节数
         *  @return 句柄；槽位耗尽或参数非法时返回 nullptr
         *  @note 无锁、无分配、**不做字符串查重**：同一 category 重复申请得到不同槽位，语义相同。
         *        句柄只在 Ready 区间内有效：`on_shutdown` 会复位全部槽位，复位后的旧句柄 `table`
         *        为 nullptr，解引用即空指针崩溃——这是契约内的误用后果，调用方必须在 `on_shutdown`
         *        里置空自己的句柄。
         */
        Logger*(*acquire_logger)(const char* category, uint32_t category_len) noexcept = nullptr;

        /**
         *  @brief 冷路径：在 Ready 区间内重建后端并应用完整配置
         *  @param config 目标配置；全部字段都允许变化（sink / async / queue_size / 轮转 / flush_on_warn /
         *                overrun_drop_oldest / file_path）
         *  @return 成功为 true。以下情况返回 false 且**旧后端与旧状态完全不变**：
         *          `NotReady`、`InvalidEnvelope`、`InvalidLevel`、`InvalidQueueSize`、`MissingFilePath`、
         *          `FileNotWritable`、`NoSink`（原因见 `LogStats::last_config_error`）
         *  @note 顺序固定：先完整校验并探测文件可写，再建出全新后端（新 sink / 新 logger / 新线程池），
         *        全部成功后一次交换，最后销毁旧后端并排空其未被丢弃的消息。
         *        只有 `min_level` / `flush_interval_ms` 变化时等价于 `configure`，不重建后端。
         *        重建意味着旧后端队列里未被丢弃的消息会先被处理完，因此本调用可能阻塞到排空完成。
         *        线程约束：宿主必须保证本调用不与 `write`/`flush`/`configure`/`get_stats`/shutdown 并发。
         */
        bool(*reconfigure)(const LogConfig& config) noexcept = nullptr;

        /**
         *  @brief 冷路径：读取运行期诊断读数
         *  @param stats 输出目标；允许为 nullptr（此时不做任何事）
         *  @note 后端可用时填当前后端的计数与拓扑；没有后端时把整个结构清零，只保留
         *        `ready = false` 与 `last_config_error`。
         *        线程约束：宿主必须保证本调用不与 `reconfigure` / shutdown 并发。
         */
        void(*get_stats)(LogStats* stats) noexcept = nullptr;

        // 级别过滤的唯一真源：调用方预检与插件内过滤读的是同一个原子量（relaxed）
        const std::atomic<uint32_t>* active_level = nullptr;

        // 本实现的接口修订，必须等于 kLogApiRevision
        uint32_t                     revision     = 0;
    };

    static_assert(std::is_trivially_copyable_v<LogConfig>,
                  "LogConfig 必须是可平凡复制的纯 POD（跨 DLL 按引用传 const 指针）");
    static_assert(std::is_trivially_copyable_v<LogStats>,
                  "LogStats 必须是可平凡复制的纯 POD（跨 DLL 由调用方分配、插件写入）");
    static_assert(std::is_trivially_copyable_v<LogApiTable>,
                  "LogApiTable 必须是可平凡复制的纯 POD（跨 DLL 只传指针，不传容器/虚表/异常）");
} // namespace ID::Log
