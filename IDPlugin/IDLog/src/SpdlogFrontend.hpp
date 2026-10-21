#pragma once

#include <atomic>
#include <cstdint>

#include "Common/Core.hpp"
#include "IDLogApi.hpp"

/**
 *  spdlog 前端的对外接口（pimpl）
 *  1. 本头 **不包含任何 spdlog 头，也不出现任何 spdlog 类型**：只有 `struct Impl;` 前置声明。
 *     header-only 的模板实例化全部压在 `SpdlogFrontend.cpp` 一个 TU 里，
 *     避免每个包含者各编译一遍 spdlog（这是硬要求，不是可选优化）；
 *  2. 级别过滤的唯一真源是 `g_active_level` 原子量，spdlog 侧的 logger 级别固定为 `trace`，
 *     因此改级别只改一个原子量，不触碰 spdlog；
 *  3. 后端句柄 `m_impl` 是 `std::atomic<Impl*>`：它只解决"指针本身"的数据竞争
 *     （write/flush 与 shutdown/reconfigure 交换指针），**不代表**这些调用可以并发。
 *     宿主仍必须保证 reconfigure / configure / get_stats / shutdown 与其它日志调用串行；
 *  4. 除 `write` 外全部是冷路径；`write` 每次只做一次 acquire load、一次拼接与一次 spdlog 提交。
 */

namespace ID::Log
{
    // pimpl：完整定义只在 SpdlogFrontend.cpp，因此本头可以用在任何不含 spdlog 的编译单元
    struct Impl;

    /**
     *  @brief 级别过滤的唯一真源（静态存储期，地址在进程内不变）
     *  @note 静态存储期 + 外部链接使它可以直接进 `constexpr` 的 `LogApiTable`：
     *        `&g_active_level` 是常量表达式，不需要任何运行期初始化。
     *        未初始化时取 `Level::Info`，即插件加载前不存在任何输出。
     */
    extern std::atomic<uint32_t> g_active_level;

    /**
     *  @brief 最近一次 init/configure/reconfigure 的失败原因（`LogConfigError` 数值）
     *  @note 只做诊断：`get_stats` 把它抄进 `LogStats::last_config_error`。写入用 relaxed。
     */
    extern std::atomic<uint32_t> g_last_config_error;

    /**
     *  @brief 从未 Ready 过就进来的写入数（进程级累计，跨 start/shutdown 不归零）
     */
    extern std::atomic<uint64_t> g_dropped_before_ready;

    /**
     *  @brief 曾经 Ready 过、后端已销毁后进来的写入数（进程级累计，跨 start/shutdown 不归零）
     */
    extern std::atomic<uint64_t> g_dropped_after_shutdown;

    /**
     *  @brief 是否曾经成功进入过 Ready（进程级累计）
     *  @note 用来把"未初始化时的丢弃"与"关闭后的丢弃"分开计数；一旦置位就不再清零。
     */
    extern std::atomic<bool> g_ever_ready;

    /**
     *  日志前端：持有 spdlog 的 logger、sink 与线程池
     *  @note 本类**没有运行期析构释放路径**：资源只由显式 `shutdown()` 释放。
     *        DLL 卸载晚于内核分配器销毁，静态析构会访问已释放的内存，因此析构函数是空操作。
     */
    class SpdlogFrontend
    {
    public:
        constexpr SpdlogFrontend() noexcept = default;
        ~SpdlogFrontend() noexcept          = default;    // 空操作：实现见 shutdown()

        SpdlogFrontend(const SpdlogFrontend&)            = delete;
        SpdlogFrontend& operator=(const SpdlogFrontend&) = delete;

        /**
         *  @brief 冷路径：校验配置并建立 sink、logger 与异步线程池
         *  @param config 目标配置；所有配置错误在这里校验并以返回值表达
         *  @return 成功为 true；已初始化、握手字段不匹配、级别越界、异步队列容量非法、
         *          文件路径缺失或不可写、或是没有任何 sink 时返回 false，且不留下任何已建立的资源
         *  @note 无异常模式下 spdlog 的失败点表现为终止进程，因此文件 sink 在构造前先探测可写；
         *        不信任调用方的 `file_path`：先把容量范围内最后一个字节写成 '\0'（按容量截断）再使用，
         *        未以 NUL 结尾的满长数组不会让 `fopen` / `std::string` 越过缓冲边界。
         *        本函数只在插件 `on_init` 内调用。
         */
        bool init(const LogConfig& config) noexcept;

        /**
         *  @brief 冷路径：flush、停线程池、释放本前端持有的全部对象
         *  @note 幂等：未初始化时直接返回。必须在 `BootstrapLog::set_sink()` 已还原之后调用，
         *        否则内核日志会调进已释放的 sink。线程池在这里析构：它以 FIFO 排空未被丢弃的消息后
         *        join，因此返回时不会再有消费者线程访问 sink。
         */
        void shutdown() noexcept;

        /**
         *  @brief 冷路径：在 Ready 区间内改变运行期可调项
         *  @param config 目标配置
         *  @return 只改了 `min_level` / `flush_interval_ms` 且取值合法时为 true，否则 false 且状态不变
         *  @note 以下字段任一变化都需要重建后端，一律拒绝（并记 `TopologyRequiresReconfigure`）：
         *        `enable_console`、`enable_file`、`async`、`queue_size`、`flush_on_warn`、
         *        `overrun_drop_oldest`、`max_file_bytes`、`max_files`、`file_path`（任何与当前取值不同的路径）。
         *        被拒的请求里的 `min_level` 不会被顺手应用。
         */
        bool configure(const LogConfig& config) noexcept;

        /**
         *  @brief 冷路径：重建后端并应用完整配置
         *  @param config 目标配置；全部字段都允许变化
         *  @return 校验通过并成功交换后端时为 true；失败时旧后端与旧状态完全不变
         *  @note 顺序固定：先校验+探测文件可写，再建全新后端（新 sink / 新 logger / 新线程池），
         *        成功后一次交换 `m_impl`，最后销毁旧后端并排空其未被丢弃的消息。
         *        只有 `min_level` / `flush_interval_ms` 变化时等价于 `configure`。
         *        线程约束：调用方必须保证不与 `write` / `flush` / `configure` / `get_stats` / `shutdown` 并发。
         */
        bool reconfigure(const LogConfig& config) noexcept;

        /**
         *  @brief 冷路径：填充运行期诊断读数
         *  @param stats 输出目标；为 nullptr 时不做任何事
         *  @note 后端可用时读当前线程池的 overrun/discard 计数并填拓扑；没有后端时整个结构清零，
         *        只保留 `ready = false` 与 `last_config_error`。
         *        线程约束：调用方必须保证不与 `reconfigure` / `shutdown` 并发。
         */
        void get_stats(LogStats* stats) noexcept;

        /**
         *  @brief 写一条日志（热路径；跨 DLL 一次间接调用之后进入本函数）
         *  @param level 级别数值；越界值收敛为 `Error`
         *  @param category 分类标签；允许为 nullptr，只在本调用内读取
         *  @param category_len category 字节数
         *  @param message 已格式化消息；允许为 nullptr
         *  @param message_len message 字节数
         *  @note 拼成 `[category] message` 后以 `spdlog::string_view_t` 提交，插件内不再走 fmt 解析；
         *        拼接超过 `kLineCapacity` 时按 UTF-8 边界截断并补 "..."。
         *        `Level::Fatal` 走同步 logger + flush + `std::abort()`，不进异步队列；
         *        后端不可用时同样走 `BootstrapLog` 的 Fatal 后 abort。
         */
        void write(uint32_t level, const char* category, uint32_t category_len,
                   const char* message, uint32_t message_len) noexcept;

        /**
         *  @brief 冷路径：排空 sink
         *  @note 同步模式直接 flush 所有 sink；异步模式投递 flush 屏障，
         *        保证屏障之前入队的消息最终会被处理，但本函数不等待屏障完成，
         *        也不保证队列排空或数据物理持久化。
         */
        void flush() noexcept;

        /**
         *  @brief 取级别过滤原子量的地址（与 `LogApiTable::active_level` 是同一个对象）
         *  @return 静态存储期的原子量指针，永不为空
         */
        ID_FORCEINLINE const std::atomic<uint32_t>* active_level() const noexcept
        {
            return &g_active_level;
        }

    private:
        // 后端指针：acquire load / release store / exchange(acq_rel)；只保护指针本身，不替代宿主串行前提
        std::atomic<Impl*> m_impl{ nullptr };
    };
} // namespace ID::Log
