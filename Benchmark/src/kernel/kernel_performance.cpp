#include "benchmark.hpp"

#include <cstdio>
#include <cstdlib>

#include "Kernel/Memory.hpp"
#include "Kernel/Plugin.hpp"
#include "Kernel/PluginRuntime.hpp"

/**
 *  ID.Kernel 性能基准：Slab 小对象、大对象页、服务槽查表 + 间接调用
 *  1. 每条指标都在同一轮、同一进程内测出对照实现（os-baseline），参考值 = 对照值 × 目标倍数；
 *  2. 单轮目标 100 ms，共 5 轮取中位数；极差超过 20% 时在 note 里标 unstable；
 *  3. 防优化：迭代结果在计时结束之后才交给 KeepAlive，绝不留"没人用"的局部变量；
 *  4. 服务槽读取走运行期 `PluginRuntime::get_service`（P4 起不再使用 PluginManager 的旧槽表）：
 *     槽表由计划里的唯一 owner 在自己的 on_register 发布、on_unload 注销，与产品路径一致；
 *  5. 服务槽指标的被测表达式与对照表达式都把函数指针经 `opaque_target` 过一次编译器屏障：
 *     否则 GCC 会把 `table->mix(...)` 推测性去虚拟化成"守卫 + 内联目标"，实测循环的默认
 *     路径就没有间接调用，读数也就不再代表"一次固定槽 load + 一次间接调用"。
 */

namespace
{
    constexpr double     kTargetRoundNs = 100.0e6;      // 单轮目标时长
    constexpr ID::size_t kMinIters      = 1u << 10;
    constexpr ID::size_t kMaxIters      = 1u << 26;
    constexpr int        kRounds        = 5;

    struct Sample
    {
        double median_ns = 0.0;     // 5 轮中位数，报告值
        double min_ns    = 0.0;
        double max_ns    = 0.0;
        double spread    = 0.0;     // (max - min) / median，用于判定 unstable
    };

    template <typename Op>
    double time_batch(Op& op, ID::size_t iters) noexcept
    {
        const ID::Bench::Timer timer;
        op(iters);
        return timer.ElapsedNs();
    }

    // 先用一小批测出单次开销，再一次性放大到目标单轮时长
    template <typename Op>
    ID::size_t calibrate(Op& op) noexcept
    {
        op(kMinIters);                                                    // 预热：缓存、分支预测、分配器页
        const double elapsed = time_batch(op, kMinIters);
        const double per_op  = elapsed > 0.0 ? elapsed / static_cast<double>(kMinIters) : 1.0;
        const double wanted  = kTargetRoundNs / (per_op > 0.0 ? per_op : 1.0);

        if (wanted <= static_cast<double>(kMinIters))
        {
            return kMinIters;
        }
        if (wanted >= static_cast<double>(kMaxIters))
        {
            return kMaxIters;
        }
        return static_cast<ID::size_t>(wanted);
    }

    template <typename Op>
    Sample measure(Op&& op) noexcept
    {
        ID::size_t iters = calibrate(op);

        // 迭代数估偏时补一次校正，避免某一轮明显偏离 100 ms
        const double probe = time_batch(op, iters);
        if (probe > 0.0 && iters < kMaxIters)
        {
            const double factor = kTargetRoundNs / probe;
            if (factor > 4.0 || factor < 0.25)
            {
                const double next = static_cast<double>(iters) * factor;
                iters = next >= static_cast<double>(kMaxIters) ? kMaxIters
                      : next <= static_cast<double>(kMinIters) ? kMinIters
                      : static_cast<ID::size_t>(next);
            }
        }

        double samples[kRounds] = {};
        for (int round = 0; round < kRounds; ++round)
        {
            samples[round] = time_batch(op, iters) / static_cast<double>(iters);
        }

        // 5 个样本直接插入排序：中位数抗单次调度抢占，均值会被拉偏
        for (int i = 1; i < kRounds; ++i)
        {
            const double key = samples[i];
            int j = i - 1;
            while (j >= 0 && samples[j] > key)
            {
                samples[j + 1] = samples[j];
                --j;
            }
            samples[j + 1] = key;
        }

        Sample sample{};
        sample.min_ns    = samples[0];
        sample.max_ns    = samples[kRounds - 1];
        sample.median_ns = samples[kRounds / 2];
        sample.spread    = sample.median_ns > 0.0 ? (sample.max_ns - sample.min_ns) / sample.median_ns : 0.0;
        return sample;
    }

    // 统一的指标记录入口：极差超 20% 时把 unstable 追加进 note
    void record(const char* name, const Sample& sample, double reference_ns,
                const char* basis, std::string_view note) noexcept
    {
        if (sample.spread > 0.20)
        {
            char buffer[384];
            std::snprintf(buffer, sizeof(buffer), "%.*s；unstable: 5 轮极差 %.0f%%",
                          static_cast<int>(note.size()), note.data(), sample.spread * 100.0);
            ID::Bench::RecordMetric({ .name         = name,
                                      .actual_ns    = sample.median_ns,
                                      .reference_ns = reference_ns,
                                      .basis        = basis,
                                      .note         = buffer });
            return;
        }

        ID::Bench::RecordMetric({ .name         = name,
                                  .actual_ns    = sample.median_ns,
                                  .reference_ns = reference_ns,
                                  .basis        = basis,
                                  .note         = note });
    }

    // ---- 被测代码：内核 Slab 小对象分配 / 释放 ----

    struct SlabAllocFree16
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                void* block = ID::Memory::ID_AlignedAlloc(16, 16);
                accumulator ^= reinterpret_cast<ID::uintptr_t>(block);
                ID::Memory::ID_AlignedFree(block);
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    struct SlabAllocFree512
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                void* block = ID::Memory::ID_AlignedAlloc(512, 16);
                accumulator ^= reinterpret_cast<ID::uintptr_t>(block);
                ID::Memory::ID_AlignedFree(block);
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    // ---- 对照实现：同轮 CRT 通用分配器 ----

    struct MallocFree16
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                void* block = std::malloc(16);
                accumulator ^= reinterpret_cast<ID::uintptr_t>(block);
                std::free(block);
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    struct MallocFree512
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                void* block = std::malloc(512);
                accumulator ^= reinterpret_cast<ID::uintptr_t>(block);
                std::free(block);
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    // ---- 大对象页：8193B 走大对象路径，实际映射一页 64KB ----

    struct LargeObjectPage
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                void* block = ID::Memory::ID_AlignedAlloc(8193, 16);
                accumulator ^= reinterpret_cast<ID::uintptr_t>(block);
                ID::Memory::ID_AlignedFree(block);
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    // 对照实现：同一条 OS 路径、同样 64KB 映射，只是不含大对象头写入与 magic 校验
    struct OsPageAllocFree
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                void* page = ID::Memory::os_alloc_aligned_64k(64 * 1024);
                accumulator ^= reinterpret_cast<ID::uintptr_t>(page);
                if (page != nullptr)
                {
                    // 与内核路径对齐：大对象会在页首写头，对照也必须碰一次页，
                    // 否则对照完全避开 demand-zero 缺页，两者就不可比
                    *static_cast<volatile ID::uint8_t*>(page) = 0x5A;
                }
                ID::Memory::os_free_aligned_64k(page, 64 * 1024);
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    // ---- 服务槽：固定槽位查表 + 一次间接调用 ----

    struct ApiTable
    {
        using TargetFn = ID::uint32_t (*)(ID::uint32_t) noexcept;

        TargetFn mix;                                        // 唯一成员：被测的唯一一次间接调用目标
    };

    ID::uint32_t mix_round(ID::uint32_t value) noexcept
    {
        return value * 2654435761u + 12345u;
    }

    constexpr ApiTable kServiceTable{ &mix_round };

    /**
     *  @brief 把已 load 的函数指针过一次空屏障，让优化器不再知道它的值
     *  @param fn 从服务表成员里读出的目标函数指针（调用前求值，不受屏障影响）
     *  @return 同一个指针
     *  @note 只用于本文件的被测/对照表达式。表对象是本 TU 的 constexpr 常量，GCC 会据此把
     *        `table->mix(...)` 推测性去虚拟化成 `cmp 守卫 + 内联 mix_round`，`call *%rdx`
     *        只留在守卫失败分支；那样实测默认路径就没有间接调用（证据见
     *        validation/perf-getservice-evidence.txt 第 4 节与第 10 节）。寄存器输入输出的空
     *        asm 屏障是唯一在本构建参数下稳定、且 LTO 下同样有效的做法：跨 TU 常量不可见只在
     *        非 LTO 构建成立。
     */
    ID_FORCEINLINE ApiTable::TargetFn opaque_target(ApiTable::TargetFn fn) noexcept
    {
        #if defined(_MSC_VER)
            // MSVC/x64 没有内联汇编：用 volatile 局部对象强制一次真实 load，
            // 优化器不能把已知值直接传下去（MSVC 分支未做反汇编核对）
            volatile ApiTable::TargetFn hidden = fn;
            return hidden;
        #else
            asm volatile("" : "+r"(fn));
            return fn;
        #endif
    }

    // ---- 运行期计划：单插件，唯一 owner 发布槽位 Math ----
    // 与产品路径一致：槽只在 owner 的 on_register 发布、on_unload 注销，计划数据是静态存储期常量
    void publish_benchmark_table() noexcept
    {
        static_cast<void>(ID::PluginRuntime::instance().publish_service(
            static_cast<ID::uint32_t>(ID::ServiceSlot::Math), &kServiceTable));
    }

    void revoke_benchmark_table() noexcept
    {
        static_cast<void>(ID::PluginRuntime::instance().revoke_service(
            static_cast<ID::uint32_t>(ID::ServiceSlot::Math)));
    }

    inline constexpr ID::PluginRuntimeCallbacks kServiceCallbacks{
        &publish_benchmark_table, nullptr, nullptr, &revoke_benchmark_table };

    inline constexpr ID::PluginRuntimeRecord kServiceRecord{
        .name      = "Bench.Kernel.ServiceHost",
        .plugin_id = ID::hash_fnv_1a64("Bench.Kernel.ServiceHost"),
        .callbacks = kServiceCallbacks
    };

    inline constexpr ID::uint16_t kServiceTopo[1] = { 0 };

    inline constexpr ID::PluginRuntimeProvision kServiceProvisions[1] = {
        { .capability = "bench.kernel.math", .api_revision = 1, .plugin_index = 0,
          .slot = static_cast<int16_t>(ID::ServiceSlot::Math) }
    };

    struct ServiceSlotOwners
    {
        ID::uint16_t cells[ID::PluginRuntime::kMaxSlots];

        constexpr ServiceSlotOwners() noexcept
            : cells{}
        {
            for (ID::size_t slot = 0; slot < ID::PluginRuntime::kMaxSlots; ++slot)
            {
                cells[slot] = ID::kNoIndex;
            }
            cells[static_cast<ID::size_t>(ID::ServiceSlot::Math)] = 0;
        }
    };

    inline constexpr ServiceSlotOwners kServiceOwners{};

    inline constexpr ID::PluginRuntimePlanView kServicePlan{
        .plugin_count    = 1,
        .provision_count = 1,
        .records         = &kServiceRecord,
        .topo_order      = kServiceTopo,
        .provisions      = kServiceProvisions,
        .slot_owner      = kServiceOwners.cells
    };

    // 对照实现：volatile 全局表指针，强制每轮真的重新 load 一次并走间接调用。
    // 光靠 volatile 不够：GCC 仍会对已知目标做守卫式去虚拟化，所以目标指针同样过 opaque_target
    constinit const ApiTable* volatile g_control_table = &kServiceTable;

    struct SlotLookupCall
    {
        void operator()(ID::size_t iters) const noexcept
        {
            const ID::PluginRuntime& runtime = ID::PluginRuntime::instance();
            ID::uint32_t             accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ApiTable*          table = runtime.get_service<ApiTable>(ID::ServiceSlot::Math);
                const ApiTable::TargetFn mix   = opaque_target(table->mix);
                accumulator += mix(static_cast<ID::uint32_t>(i));
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };

    struct GlobalTableCall
    {
        void operator()(ID::size_t iters) const noexcept
        {
            ID::uint32_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ApiTable*          table = g_control_table;
                const ApiTable::TargetFn mix   = opaque_target(table->mix);
                accumulator += mix(static_cast<ID::uint32_t>(i));
            }
            ID::Bench::KeepAlive(&accumulator);
        }
    };
} // namespace

namespace ID::Bench
{
    void IDKernelPerformanceTest() noexcept
    {
        // 服务槽必须先由计划里的 owner 发布，否则查表取到 nullptr。
        // 只有失败才输出判定行：健康路径不往指标表里插 PASS 行，失败则明确报错并计入退出码
        ID::PluginRuntime&                runtime = ID::PluginRuntime::instance();
        const ID::PluginRuntimeDiagnostic started = runtime.start(kServicePlan);
        if (!started.is_ok())
        {
            ID::Bench::Check(false, "perf.service_plan.start",
                             "计划内的唯一 owner 必须发布槽位并进入 Ready，服务槽指标才有意义");
            return;
        }

        // ---------- 小对象 16B：对照 std::malloc(16) ----------
        {
            MallocFree16    malloc_baseline;
            SlabAllocFree16 slab_alloc;
            const double    malloc_ns = measure(malloc_baseline).median_ns;
            const Sample    slab      = measure(slab_alloc);
            record("memory.alloc_free_16B", slab, malloc_ns * 1.0, "os-baseline",
                   "同轮 std::malloc(16)+free 实测；目标倍数 1.0（至少要打平通用分配器）");
        }

        // ---------- 小对象 512B：对照 std::malloc(512) ----------
        {
            MallocFree512    malloc_baseline;
            SlabAllocFree512 slab_alloc;
            const double     malloc_ns = measure(malloc_baseline).median_ns;
            const Sample     slab      = measure(slab_alloc);
            record("memory.alloc_free_512B", slab, malloc_ns * 1.0, "os-baseline",
                   "同轮 std::malloc(512)+free 实测；目标倍数 1.0（至少要打平通用分配器）");
        }

        // ---------- 大对象页：对照同尺寸的 os_alloc_aligned_64k ----------
        {
            OsPageAllocFree os_page;
            LargeObjectPage large_object;
            const double    os_page_ns = measure(os_page).median_ns;
            const Sample    large      = measure(large_object);
            record("memory.large_object_page", large, os_page_ns * 1.0, "os-baseline",
                   "同轮 os_alloc_aligned_64k(64KB)+free 实测（同一 OS 路径，缺大对象头写入与 magic 校验）；目标倍数 1.0");
        }

        // ---------- 服务槽查表 + 间接调用：对照 volatile 全局表指针 ----------
        {
            GlobalTableCall control_call;
            SlotLookupCall  slot_call;
            const double    control_ns = measure(control_call).median_ns;
            const Sample    slot       = measure(slot_call);
            record("plugin.service_lookup_and_call", slot, control_ns * 1.2, "os-baseline",
                   "被测路径 = get_service 一次固定槽 load + 表成员 load + 一次真间接调用，"
                   "对照 = 同轮 volatile 全局表指针 + 同样一次真间接调用（两侧函数指针都过空 asm 屏障，"
                   "GCC 不能去虚拟化）；目标倍数 1.2（多一次槽位数组 load ≈ L1 4-5 cycles）");
        }

        // 关闭：on_unload 注销自己发布的槽，关闭后槽表必须为空；失败同样只输出判定行
        const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
        if (!stopped.is_ok())
        {
            ID::Bench::Check(false, "perf.service_plan.shutdown",
                             "运行期关闭必须回到 Idle 并清空槽位");
        }
    }
} // namespace ID::Bench
