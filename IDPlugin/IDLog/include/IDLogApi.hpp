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
 *     `[category] message` 拼接，不再解析格式串；
 *  5. **Ready 区间**（本头统一术语）：前端（本插件）的 `on_init` 已成功返回，且调用方处于自己的 `on_init` 序内时
 *     即成立，窗口延续到本插件 `on_shutdown` 复位槽位之前。它**不依赖**内核的运行期状态：下游处于自己的
 *     `on_init` 序内时运行期状态仍是 `Initializing`，此时读服务表与 `acquire_logger` 都合法。本插件不提供
 *     engineReady 状态位、运行期就绪检查或 `awaitEngineReady` 一类等待接口；
 *  6. **ABI 纪律**：本头是 rev2 的冻结面，**不得**直接加字段、改布局或扩展 `LogConfigError` 取值集合。
 *     失败码与 `LogStats` 的任何变更都必须**批量**进入 rev3 提案（同时写明 `version` 与 `api_revision`
 *     各自是否变化、`struct_size` envelope 如何同步）；`revision` 始终等于 `plugin.json` 的 `provides.api_revision`。
 *  7. **错误分类**（两级，分界线是"有没有返回值可等"）：
 *     - **可恢复**：非法配置（握手字段、级别、队列容量、缺路径、无 sink 组合）与探写失败（权限不足、
 *       目录不存在）都由 `configure` / `reconfigure` 返回 `false` 表达，原因写进 `LogStats::last_config_error`；
 *       它们只承诺"旧后端与旧运行期状态不变"，**不**承诺文件系统零副作用（探写会以追加模式打开、可能创建文件）。
 *     - **不可恢复**：内存分配失败（后端对象都经分配器创建，失败时拿到空指针，构造不会给出可恢复的失败）
 *       与无异常构建下 vendor 的任何失败点（file sink 构造、写文件、`fflush`、轮转重命名）都**没有返回值可等**：
 *       进程不会回到调用方，也不会退化成"丢弃 + 计数"。`write` 更没有返回值，运行期 I/O 失败按 sink 分流
 *       （见该入口注记）。
 *     - **探写是 TOCTOU，不是保证**：`fopen(path, "ab")` 只证明"此刻能打开"；从探写到真正构造 sink 之间，
 *       磁盘配额、权限与路径都可能变化，因此探写成功不保证后端构造之后仍可写，探写失败也不代表之后不可写。
 *  8. **完成语义**：`submitted` / `processed` / `discarded` / `sink_flushed` / `durable` 五态互不等价，
 *     `durable` 在 v1 是**非目标**；`drain(quiescent)` 与 `barrier + ack` 属 **rev3 提案**（v1 不实现、
 *     不改变 `flush()` 行为）：其契约是"控制消息不可丢弃"，且等待超时或返回失败时，调用方**不得**
 *     销毁仍在使用中的资源（前端、线程池、sink、句柄）。v1 没有排空入口，`flush` 也不能代替它。
 *  9. **计数闭合**：`overrun_oldest` 下 `accepted = delivered + dropped + inflight` 必须闭合；
 *     这类计数只允许在冷路径（`get_stats`）或原始验证中读取，**禁止**为它在热路径新增无条件累加。
 *  10. **未取证的边界**（契约已写、实测证据未取，采用时按"未验证"对待，不得写成保证）：
 *      a. **自举日志桥的重入 `Fatal`**：桥内再次报告 `Fatal` 时不再转交 `BootstrapLog`，而是把这一行
 *         直接写 `stderr` 后终止。现有校验资产（终止语义校验、覆盖启动回滚/配置失败/槽位复用的行为驱动、
 *         四配置矩阵）里的 `Fatal` 用例都是首次进入桥，重入分支未被执行；要命中它必须**注入**
 *         （后端内再回调一次 Fatal 报告）。
 *      b. **卸载期的静态析构时序**：前端析构是空操作、资源只由关闭流程释放，因此 `on_unload` 之后
 *         不再依赖任何静态析构（DLL 卸载晚于内核分配器销毁）。该时序差值发生在 `FreeLibrary` / 进程退出
 *         阶段，驱动进程内无法在"分配器已销毁、DLL 仍在执行代码"的窗口里断言，需进程外探针或分配器日志。
 *      c. **`category` 悬垂读取**：`acquire_logger` 只保存指针，调用方提前释放或就地改写后句柄读到的是
 *         悬空/脏数据。这是调用方违约后的后果，字节取决于堆复用、不可稳定断言，只能做非确定性观测
 *         （或改用页保护 / 影子分配器把它变成必现故障）。
 *      取证条件与口径见 `README.md` 的「1.5 未取证的行为项」。
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
         *        因此对**合法 UTF-8 输入**而言输出是"合法 UTF-8 前缀 + ..."，不会留下半个码点；
         *        它**不修复**调用方已经写进缓冲的非法 UTF-8 字节（那不在这条规则的保证范围内）。
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
     *  @note 队列满是两种策略的代价**不同**，都不构成"不阻塞"：
     *        - `overrun_drop_oldest == true`（`overrun_oldest`）：覆盖最旧一条，不等待队列空位，
     *          但入队**仍要取队列互斥量**，因此它**不是 wait-free**，短时锁竞争依然存在；
     *        - `overrun_drop_oldest == false`（`block`）：队列满时**生产者等待空位**（无超时、无返回值、
     *          也不计丢弃），饱和时压力直接变成调用线程的阻塞时间。
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
        bool        overrun_drop_oldest = true;     // 队列满丢最旧；不等待空位，但仍会取队列锁（两种策略的代价见上方注记）
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
     *        并且要求宿主保证不与关闭并发（见各自注记）。
     *        本表**没有 shutdown 入口**：关闭由宿主的 `ID::Engine::shutdown()` 驱动（对应本插件
     *        `on_shutdown`）。关闭契约是"宿主先停掉所有会写日志的生产者，再串行关闭"：
     *        表里任何入口都不得与 `ID::Engine::start()` / `ID::Engine::shutdown()` 并发。
     *        关闭只保证"**未被丢弃**的 FIFO 消息被处理"（交给 sink），不保证输出成功：
     *        任意 I/O 失败时不得把结果称作"全部持久化"（`durable` 在 v1 是非目标）。
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
         *  @note `category` / `message` 为 `nullptr` 时该段不写入、不计长度：行的长度永远是**真实写入**的字节数，
         *        不会把缓冲尾部未初始化的内容当成消息（`write(lv, nullptr, 2000, "x", 1)` 只产生 `[] x`）。
         *        行超过 `kLineCapacity` 时截断并补 "..."，规则与调用侧一致：只保证合法 UTF-8 输入的前缀 + "..."。
         *  @note `Level::Fatal` 无条件终止：同步写 + flush 后 `std::abort()`，不进异步队列，
         *        也不受 `active_level` 过滤。后端不可用（未初始化 / 已关闭）时同样终止：
         *        消息转交 `BootstrapLog` 的 Fatal 后 abort。Fatal **不排空**主队列，与在途异步消息也没有
         *        全局顺序保证（`abort` 时尚未被 worker 处理的消息可能不落地），任意 I/O 失败也不保证这一行输出成功。
         *  @note **运行期 I/O 失败没有可返回的错误通道**（本入口的返回类型是 `void`）：
         *        - console sink 的写失败被**忽略**：那一行直接丢失，进程继续，也没有计数可读；
         *        - 文件 sink 的写失败与轮转重命名失败会**终止进程**（无异常构建下后端的失败点先打印一行
         *          错误再 `abort()`），不会退化成"丢弃并计数"。
         *        v1 **没有**把运行期 I/O 失败降级为错误计数的路径：`LogStats` 没有这类字段，rev2 头也不允许
         *        加字段，因此"写失败 -> 计数 + 非重入 stderr best-effort"整体属 **rev3 提案**，
         *        本轮只做事实披露。`BootstrapLog` 的 stderr best-effort 只覆盖 Fatal 回退路径
         *        （带线程局部重入保护），不是通用的 I/O 失败通道。
         *        因此 **任何 I/O 失败都不保证 `Fatal` 成功**，也不保证 `Fatal` 之外的行落地；
         *        需要"写失败可观测 / 可恢复"的场合必须另建通道，不要把本入口当成能观测自身写失败的入口。
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
         *  @note 返回 false 时只承诺"运行期状态不变"：被拒的请求里的 `min_level` 不会被顺手应用。
         *        越界 `min_level` 同样返回 false，不接受把非法整数当成"全部打开"。
         *        本入口**不做文件探测**（没有 `fopen`），因此被拒的请求不会创建或触碰目标文件；
         *        需要重建后端（含探写）的请求请改用 `reconfigure`（其副作用边界见该入口注记）。
         *        失败原因写进 `LogStats::last_config_error`。
         *        线程约束：宿主必须保证本调用不与 `write`/`flush`/`reconfigure`/`get_stats`/
         *        `ID::Engine::shutdown()` 并发。
         */
        bool(*configure)(const LogConfig& config) noexcept = nullptr;

        /**
         *  @brief 冷路径：把 sink 内容刷给 OS（**控制屏障**，不等待、不是完成语义）
         *  @note 同步形态：直接 flush 两个 logger 的 sink，返回时内容已交给 OS（`fflush` 语义，不是 `fsync`）。
         *        异步形态：只向主 logger **投递一条控制屏障**，本调用**不等待**、不排空队列、不触碰物理介质；
         *        随后在调用线程上 flush 同步 logger 的 sink（只覆盖已进 sink 的那部分内容）。
         *        `overrun_drop_oldest == true` 时屏障本身也是一条队列消息，**可能被后续消息覆盖**：
         *        屏障之前已提交但未被 worker 处理的消息因此**不保证落地**，屏障之后的因果顺序同样不保证；
         *        `overrun_drop_oldest == false` 时屏障按阻塞策略入队、不会自身被覆盖，但本调用仍不等待，
         *        返回时队列可能远未排空，同样不构成完成语义。
         *        禁止把 `flush` 当成"至少保证已有消息处理完"或"已落盘"的断言点：它不提供 `drained` 或 `durable` 语义。
         *  @note 完成语义只有五态区分：`submitted`（已提交给前端/已入队）、`processed`（已交给 sink）、
         *        `discarded`（被覆盖或显式丢弃）、`sink_flushed`（已交给 OS，`fflush` 级）、
         *        `durable`（已写入物理介质，`fsync` 级）。五态互不等价，`durable` 在本版本是**非目标**。
         *        需要"排空后确认"的场合只能等 **rev3 提案**的 `drain(quiescent)` 或 `barrier + ack` 入口：
         *        v1 不实现、也不改变本函数行为；该提案要求控制消息不可丢弃，且等待超时或返回失败时
         *        调用方不得销毁仍在使用中的资源（前端、线程池、sink、句柄）。
         *        线程约束：宿主必须保证不与 `ID::Engine::shutdown()` 并发。
         */
        void(*flush)() noexcept = nullptr;

        /**
         *  @brief 冷路径：为一个 category 申请 `Logger` 槽位
         *  @param category 分类标签；允许为 nullptr（此时返回 nullptr），只保存指针，不复制内容
         *  @param category_len category 字节数
         *  @return 句柄；槽位耗尽或参数非法时返回 nullptr
         *  @note 只在 **Ready 区间**内调用：前端 `on_init` 成功后，下游在自己的 `on_init` 序内即可调用
         *        （此时运行期状态仍是 `Initializing`，不需要、也不存在额外的就绪检查）。
         *        无锁、无分配、**不做字符串查重**：同一 category 重复申请得到不同槽位，语义相同。
         *        槽位固定 `kMaxLoggerHandles = 64` 个：本插件 `on_init` 自占 1 个，产品最多再申请 **63** 个；
         *        游标只增不回退，耗尽之后同一轮里的申请同样返回 nullptr，且**耗尽必须可观测**：
         *        调用方要检查返回值，把 nullptr 当"本模块没有日志能力"的显式分支处理，不静默继续；
         *        没有扩容入口，也没有等待重试。
         *        `category` 只保存指针、不复制内容：必须是字面量，或调用方自己持有到句柄失效为止的稳定存储；
         *        提前释放或就地改写会让句柄读到悬空/脏数据。
         *        句柄只在 Ready 区间内有效：`on_shutdown` 会复位全部 64 个槽并把游标归零，复位后的旧句柄
         *        `table` 为 nullptr，解引用即空指针崩溃——这是契约内的误用后果，调用方必须置空自己的句柄。
         *        下一轮 `start` **复用同一批槽位**，所以跨 `shutdown` 保留的旧句柄还可能别名到别的模块的槽
         *        （指向别人的 category）：**旧句柄在 shutdown 之后一律不安全**，任何跨轮复用都不在契约内。
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
         *        重建意味着旧后端队列里未被丢弃的消息会先被处理完，因此本调用可能阻塞到排空完成；
         *        这里的"处理完"指交给 sink，**不**指输出成功或已持久化（见 `flush` 的完成语义）。
         *  @note 返回 false **只**保证"旧后端与旧运行期状态不变"，**不**保证文件系统零副作用：
         *        校验阶段会用 `fopen(path, "ab")` 探写，探写本身就会**创建**目标文件；若失败发生在探写之后
         *        （例如随后构造新后端时内存不足），该文件仍会留在磁盘上。探写同时是 **TOCTOU**：
         *        它只证明"此刻能打开"，不保证随后构造 file sink 时仍可写。
         *  @note 已知缺口（v1）：`max_files` 没有上界校验，超过 vendor 上限（200000）会在建后端时进入
         *        无异常终止点，而不是返回 `false`；`LogConfigError` 目前没有对应类别，补一个"配置非法"类失败码
         *        必须批量进入 rev3 提案（rev2 头不得扩枚举）。
         *        线程约束：宿主必须保证本调用不与 `write`/`flush`/`configure`/`get_stats`/shutdown 并发。
         */
        bool(*reconfigure)(const LogConfig& config) noexcept = nullptr;

        /**
         *  @brief 冷路径：读取运行期诊断读数
         *  @param stats 输出目标；允许为 nullptr（此时不做任何事）
         *  @note 后端可用时填当前后端的计数与拓扑；没有后端时把整个结构清零，只保留
         *        `ready = false` 与 `last_config_error`。
         *        读 `overrun_dropped` / `discarded` **会取队列互斥量**（`overrun_counter()`），因此本入口必须在
         *        Ready 区间内、且由宿主串行调用：**不得**与 `reconfigure` / shutdown 并发，否则会在后端销毁过程中
         *        读已释放的队列。写者（`write`）可以并发，插件不为本入口加锁兜底。
         *        计数只用于观测，不参与任何正确性判断；不要把它放进帧循环（见 `IDLogLogger.hpp` 的调用侧说明）。
         *        正因为"没有后端就清零"，`dropped_before_ready` / `dropped_after_shutdown` 这两个进程级计数
         *        在"后端已销毁"期间读不到：它们只能在下一次 Ready 区间里被观测到。
         *  @note 计数闭合（`overrun_drop_oldest == true` 时的边界关系）：设 `accepted` 为调用方提交成功的条数、
         *        `delivered` 为已被 worker 交给 sink 的条数、`dropped` 为被覆盖（`overrun_dropped`）与显式丢弃
         *        （`discarded`）之和、`inflight` 为已提交但尚未交给 sink 的条数，则
         *        `accepted = delivered + dropped + inflight` 必须闭合；`dropped` 单调不减，
         *        `inflight` 的上界是"队列容量 + worker 手上最多 1 条"（由后端结构决定，不是字段）。
         *        `LogStats` 只提供 `overrun_dropped` 与 `discarded`，因此 `accepted` / `delivered` / `inflight`
         *        只能由调用方自己的提交计数与 sink 侧观测配对**反解**，属原始验证口径（这三项进 `LogStats`
         *        属 rev3 ABI 提案，rev2 不加字段）。
         *        这类计数只允许在冷路径（本入口）或原始验证里读取，**禁止**为它在热路径新增无条件累加。
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
