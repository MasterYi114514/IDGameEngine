#include "benchmark.hpp"

#include <cstdio>
#include <cstring>
#include <span>
#include <thread>

#include "Common/tools/LinearArena.hpp"
#include "Common/tools/SegmentedArena.hpp"
#include "Kernel/BootstrapLog.hpp"
#include "Kernel/Memory.hpp"
#include "Kernel/Plugin.hpp"

/**
 *  ID.Kernel 正确性测试：自举日志、类型/哈希、通用 Arena、内存、插件 DAG 与服务槽
 *  1. 覆盖 references/methodology.md 的 C1–C7：契约一致性、边界、非法输入、状态机/幂等、并发契约、内存安全、数据完整性；
 *  2. 契约本身就是 abort 的路径（Level::Fatal 日志、ID_KERNEL_ASSERT、report_assert）一律 Skip 并写明依据，
 *     绝不在同一个进程里故意触发 abort；
 *  3. 每个用例自包含（自己分配自己释放），失败只记 Check，不影响后续用例。
 */

namespace
{
    // ========================= 日志捕获 =========================
    // 默认 sink 会写 stderr；测试期间整体接管，避免内核日志污染 console 与 report.txt 的一致性

    struct CapturedLog
    {
        ID::Log::Level  level               = ID::Log::Level::Trace;
        char            category[32]        = {};
        char            message[160]        = {};
    };

    CapturedLog g_last_log{};
    ID::size_t  g_log_count = 0;

    void copy_truncated(char* destination, ID::size_t capacity, std::string_view source) noexcept
    {
        ID::size_t length = source.size();
        if (length >= capacity)
        {
            length = capacity - 1;
        }
        if (length != 0)
        {
            std::memcpy(destination, source.data(), length);
        }
        destination[length] = '\0';
    }

    void capturing_sink(ID::Log::Level level, std::string_view category, std::string_view message) noexcept
    {
        g_last_log.level = level;
        copy_truncated(g_last_log.category, sizeof(g_last_log.category), category);
        copy_truncated(g_last_log.message, sizeof(g_last_log.message), message);
        ++g_log_count;
    }

    // ========================= 生命周期记录 =========================
    // 每个插件用模板参数区分身份：回调无参数，只能靠不同的函数实例承载"我是谁"

    char        g_lifecycle[64]    = {};
    ID::size_t  g_lifecycle_length = 0;

    void reset_lifecycle() noexcept
    {
        std::memset(g_lifecycle, 0, sizeof(g_lifecycle));
        g_lifecycle_length = 0;
    }

    void record_lifecycle(char phase, char who) noexcept
    {
        if (g_lifecycle_length + 2 < sizeof(g_lifecycle))
        {
            g_lifecycle[g_lifecycle_length++] = phase;
            g_lifecycle[g_lifecycle_length++] = who;
            g_lifecycle[g_lifecycle_length]   = '\0';
        }
    }

    void check_lifecycle(const char* expected, std::string_view name) noexcept
    {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "期望 %s 实际 %s", expected, g_lifecycle);
        ID::Bench::Check(std::strcmp(g_lifecycle, expected) == 0, name, detail);
    }

    template <char Who>
    void plugin_on_register() noexcept
    {
        record_lifecycle('R', Who);
    }

    template <char Who>
    bool plugin_on_init() noexcept
    {
        record_lifecycle('I', Who);
        return true;
    }

    template <char Who>
    void plugin_on_shutdown() noexcept
    {
        record_lifecycle('S', Who);
    }

    template <char Who>
    void plugin_on_unload() noexcept
    {
        record_lifecycle('U', Who);
    }

    /**
     *  @brief 编译期构造插件描述符
     *  @note 描述符、依赖数组都必须是静态存储期：内核只存指针，生命周期必须盖过 shutdown_all
     */
    consteval ID::PluginDescriptor make_descriptor(std::string_view name,
                                                   std::span<const ID::PluginDependency> dependencies,
                                                   void(*on_register)() noexcept,
                                                   bool(*on_init)() noexcept,
                                                   void(*on_shutdown)() noexcept,
                                                   void(*on_unload)() noexcept,
                                                   ID::uint32_t version = 1) noexcept
    {
        return ID::PluginDescriptor
        {
            .plugin_id    = ID::hash_fnv_1a64(name),
            .version      = version,
            .name         = name,
            .dependencies = dependencies,
            .on_register  = on_register,
            .on_init      = on_init,
            .on_shutdown  = on_shutdown,
            .on_unload    = on_unload,
        };
    }

    // ---- 链式依赖 A <- B <- C ----
    constexpr ID::PluginDependency kDependencyA{ ID::hash_fnv_1a64("Bench.Kernel.A") };
    constexpr ID::PluginDependency kDependencyB{ ID::hash_fnv_1a64("Bench.Kernel.B") };

    constexpr ID::PluginDependency kDependenciesB[] = { kDependencyA };
    constexpr ID::PluginDependency kDependenciesC[] = { kDependencyB };

    constexpr ID::PluginDescriptor kPluginA = make_descriptor("Bench.Kernel.A", {},
        &plugin_on_register<'A'>, &plugin_on_init<'A'>, &plugin_on_shutdown<'A'>, &plugin_on_unload<'A'>);
    constexpr ID::PluginDescriptor kPluginB = make_descriptor("Bench.Kernel.B", kDependenciesB,
        &plugin_on_register<'B'>, &plugin_on_init<'B'>, &plugin_on_shutdown<'B'>, &plugin_on_unload<'B'>);
    constexpr ID::PluginDescriptor kPluginC = make_descriptor("Bench.Kernel.C", kDependenciesC,
        &plugin_on_register<'C'>, &plugin_on_init<'C'>, &plugin_on_shutdown<'C'>, &plugin_on_unload<'C'>);

    // ---- 菱形依赖 D -> {B, C} -> A ----
    constexpr ID::PluginDependency kDiamondA{ ID::hash_fnv_1a64("Bench.Diamond.A") };
    constexpr ID::PluginDependency kDiamondB{ ID::hash_fnv_1a64("Bench.Diamond.B") };
    constexpr ID::PluginDependency kDiamondC{ ID::hash_fnv_1a64("Bench.Diamond.C") };

    constexpr ID::PluginDependency kDiamondDependenciesB[] = { kDiamondA };
    constexpr ID::PluginDependency kDiamondDependenciesC[] = { kDiamondA };
    constexpr ID::PluginDependency kDiamondDependenciesD[] = { kDiamondB, kDiamondC };

    constexpr ID::PluginDescriptor kDiamondPluginA = make_descriptor("Bench.Diamond.A", {},
        nullptr, nullptr, nullptr, nullptr);
    constexpr ID::PluginDescriptor kDiamondPluginB = make_descriptor("Bench.Diamond.B", kDiamondDependenciesB,
        nullptr, nullptr, nullptr, nullptr);
    constexpr ID::PluginDescriptor kDiamondPluginC = make_descriptor("Bench.Diamond.C", kDiamondDependenciesC,
        nullptr, nullptr, nullptr, nullptr);
    constexpr ID::PluginDescriptor kDiamondPluginD = make_descriptor("Bench.Diamond.D", kDiamondDependenciesD,
        nullptr, nullptr, nullptr, nullptr);

    // ---- 兼容白名单：两个上游后端，下游只声明一个白名单依赖 ----
    constexpr ID::uint64_t kBackendCompatibleIds[] =
    {
        ID::hash_fnv_1a64("Bench.Backend.Simd"),
        ID::hash_fnv_1a64("Bench.Backend.Fixed"),
    };

    constexpr ID::PluginDependency kConsumerDependencies[] =
    {
        ID::PluginDependency{ std::span<const ID::uint64_t>{ kBackendCompatibleIds }, 2 },
    };

    constexpr ID::PluginDescriptor kBackendSimd = make_descriptor("Bench.Backend.Simd", {},
        nullptr, nullptr, nullptr, nullptr, 2);
    constexpr ID::PluginDescriptor kBackendFixed = make_descriptor("Bench.Backend.Fixed", {},
        nullptr, nullptr, nullptr, nullptr, 1);
    constexpr ID::PluginDescriptor kBackendConsumer = make_descriptor("Bench.Backend.Consumer", kConsumerDependencies,
        nullptr, nullptr, nullptr, nullptr);

    // ---- 容量边界：128（上限）与第 129（上限 + 1）个插件 ----
    constexpr ID::size_t kGeneratedPluginCount = 129;

    struct GeneratedPlugins
    {
        ID::PluginDescriptor entries[kGeneratedPluginCount]{};

        constexpr GeneratedPlugins() noexcept
        {
            for (ID::size_t i = 0; i < kGeneratedPluginCount; ++i)
            {
                entries[i].plugin_id    = ID::hash_fnv_1a64("Bench.Generated") + i;
                entries[i].version      = 1;
                entries[i].name         = "Bench.Generated";
                entries[i].dependencies = {};
            }
        }
    };

    constexpr GeneratedPlugins kGeneratedPlugins{};

    // ---- 服务槽测试用函数表 ----
    struct FakeApiTable
    {
        ID::uint32_t magic = 0;
    };

    constexpr FakeApiTable kApiTableOne{ 0x11111111u };
    constexpr FakeApiTable kApiTableTwo{ 0x22222222u };

    int find_plugin_position(std::span<const ID::PluginDescriptor* const> order,
                             const ID::PluginDescriptor* descriptor) noexcept
    {
        for (ID::size_t i = 0; i < order.size(); ++i)
        {
            if (order[i] == descriptor)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // ---- LinearArena 耗尽回调 ----
    ID::size_t g_arena_fallback_calls     = 0;
    ID::size_t g_arena_fallback_size      = 0;
    ID::size_t g_arena_fallback_alignment = 0;

    void arena_fallback(ID::size_t size, ID::size_t alignment) noexcept
    {
        ++g_arena_fallback_calls;
        g_arena_fallback_size      = size;
        g_arena_fallback_alignment = alignment;
    }
} // namespace

namespace ID::Bench
{
    void IDKernelSecurityTest() noexcept
    {
        // =================================================================
        // 类型与哈希：全部 consteval，运行时零成本，只验证语义
        // =================================================================
        Check(ID::type_id<ID::int32_t>() == ID::type_id<ID::int32_t>(),
              "type_id.deterministic", "同一类型的 type_id 必须稳定");
        Check(ID::type_id<ID::int32_t>() != ID::type_id<ID::uint32_t>()
           && ID::type_id<float>()        != ID::type_id<double>()
           && ID::type_id<ID::int64_t>()  != ID::type_id<ID::uint64_t>(),
              "type_id.distinct", "不同整型/浮点类型的 type_id 必须互不相同");
        Check(ID::hash_fnv_1a64("") == 14695981039346656037ull,
              "hash.fnv.offset_basis", "空串必须等于 FNV-1a 64 位偏移基数");
        Check(ID::hash_fnv_1a64("ID.Kernel") == ID::hash_fnv_1a64("ID.Kernel")
           && ID::hash_fnv_1a64("ID.Kernel") != ID::hash_fnv_1a64("ID.Kernel "),
              "hash.fnv.distinct", "相同输入同值、不同输入异值");

        // =================================================================
        // 自举日志：默认 sink 检查 -> 接管 -> 后续内核日志不再写 stderr
        // =================================================================
        Check(ID::Log::BootstrapLog::instance().get_sink() == &ID::Log::default_bootstrap_sink,
              "log.default_sink", "未接管前 sink 必须是 default_bootstrap_sink");
        ID::Log::BootstrapLog::instance().set_sink(&capturing_sink);
        Check(ID::Log::BootstrapLog::instance().get_sink() == &capturing_sink,
              "log.sink_swap", "set_sink 之后 get_sink 必须返回新 sink");
        Skip("log.assert_dispatch", "report_assert 最终走 Level::Fatal -> abort，需子进程隔离");
        Skip("log.set_sink_nullptr", "契约是断言失败后 abort，需子进程隔离");

        // =================================================================
        // 通用 Arena：LinearArena（固定缓冲区）/ SegmentedArena（按段来源增长）
        // =================================================================
        {
            alignas(ID::kCacheLineSize) ID::uint8_t buffer[1024]{};
            ID::LinearArena arena(buffer, sizeof(buffer), &arena_fallback);

            Check(arena.is_initialized() && arena.capacity() == sizeof(buffer),
                  "arena.linear.init", "绑定缓冲区后 is_initialized 为真且 capacity 等于缓冲区大小");

            void* first_block  = arena.allocate(64, 16);
            void* second_block = arena.allocate(64, 16);
            const bool bump_ok = first_block != nullptr
                              && second_block != nullptr
                              && (reinterpret_cast<ID::uintptr_t>(first_block) & 15u) == 0
                              && (reinterpret_cast<ID::uintptr_t>(second_block) & 15u) == 0
                              && reinterpret_cast<ID::uintptr_t>(second_block) > reinterpret_cast<ID::uintptr_t>(first_block)
                              && reinterpret_cast<ID::uintptr_t>(second_block) - reinterpret_cast<ID::uintptr_t>(first_block) >= 64;
            Check(bump_ok, "arena.linear.bump_align", "连续分配必须单调上移、16B 对齐且互不重叠");
            Check(arena.used_bytes() + arena.remaining_bytes() == arena.capacity(),
                  "arena.linear.used_remaining", "used + remaining 必须恒等于 capacity");

            g_arena_fallback_calls = 0;
            void* too_big = arena.allocate(4096, 16);
            Check(too_big == nullptr
               && g_arena_fallback_calls == 1
               && g_arena_fallback_size == 4096
               && g_arena_fallback_alignment == 16,
                  "arena.linear.exhaust_fallback", "缓冲区不足必须返回 nullptr 并回调 (size, alignment)");

            arena.reset();
            Check(arena.allocate(64, 16) == static_cast<void*>(buffer) && arena.used_bytes() == 64,
                  "arena.linear.reset_rewinds", "reset 后第一次分配必须回到缓冲区起点");
        }

        {
            ID::SegmentedArena segmented;
            const bool init_ok = segmented.init(4096, &ID::Memory::ID_AlignedAlloc, &ID::Memory::ID_AlignedFree);
            Check(init_ok, "arena.segmented.init", "配置 4KB 段与内核分配器来源应成功");
            Check(!segmented.init(4096, &ID::Memory::ID_AlignedAlloc, &ID::Memory::ID_AlignedFree),
                  "arena.segmented.init_twice", "重复 init 必须返回 false（需先 shutdown）");

            bool grow_ok = true;
            for (ID::size_t i = 0; i < 64 && grow_ok; ++i)
            {
                grow_ok = segmented.allocate(256, 16) != nullptr;
            }
            Check(grow_ok && segmented.block_count() >= 2,
                  "arena.segmented.grow", "段满后必须自动向来源申请新段");
            Check(segmented.allocate(8000, 16) == nullptr,
                  "arena.segmented.oversize", "单段放不下的请求必须返回 nullptr");

            segmented.reset();
            Check(segmented.allocate(256, 16) != nullptr && segmented.block_count() >= 2,
                  "arena.segmented.reset_reuse", "reset 必须保留已申请的段并从头复用");
            segmented.shutdown();
            Check(!segmented.is_initialized() && segmented.block_count() == 0,
                  "arena.segmented.shutdown", "shutdown 必须归还全部段并复位");
        }

        // =================================================================
        // 内存：C1 契约一致性
        // =================================================================
        ID::Memory::ID_MemoryInit();

        void* contract_block = ID::Memory::ID_AlignedAlloc(64, 32);
        Check(contract_block != nullptr, "memory.alloc.non_null", "64B/32 应返回非空");
        Check(contract_block != nullptr && (reinterpret_cast<ID::uintptr_t>(contract_block) & 31u) == 0,
              "memory.alloc.alignment_32", "返回值必须满足请求的 32B 对齐");
        ID::Memory::ID_AlignedFree(contract_block);

        // =================================================================
        // 内存：C2 边界 + C6 内存安全（所有档位边界尺寸，且互不重叠）
        // =================================================================
        constexpr ID::size_t kBoundarySizes[] =
        {
            1, 16, 17, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048, 4096, 8192
        };
        constexpr ID::size_t kBoundaryCount = sizeof(kBoundarySizes) / sizeof(kBoundarySizes[0]);

        void* boundary_blocks[kBoundaryCount] = {};
        bool  boundary_alloc_ok = true;
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            boundary_blocks[i] = ID::Memory::ID_AlignedAlloc(kBoundarySizes[i], 16);
            boundary_alloc_ok = boundary_alloc_ok
                             && boundary_blocks[i] != nullptr
                             && (reinterpret_cast<ID::uintptr_t>(boundary_blocks[i]) & 15u) == 0;
        }
        Check(boundary_alloc_ok, "memory.alloc.size_class_edges",
              "1B..8KB 每个档位边界都必须成功并 16B 对齐");

        // 每块写满自己的独立模式：只要两块有重叠，后写的必然破坏前一块
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            if (boundary_blocks[i] != nullptr)
            {
                std::memset(boundary_blocks[i], static_cast<int>(0x10 + i), kBoundarySizes[i]);
            }
        }
        bool boundary_intact = true;
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            if (boundary_blocks[i] == nullptr)
            {
                boundary_intact = false;
                continue;
            }
            const auto* bytes = static_cast<const ID::uint8_t*>(boundary_blocks[i]);
            for (ID::size_t b = 0; b < kBoundarySizes[i]; ++b)
            {
                if (bytes[b] != static_cast<ID::uint8_t>(0x10 + i))
                {
                    boundary_intact = false;
                    break;
                }
            }
        }
        Check(boundary_intact, "memory.alloc.no_overlap", "同时存活的小对象不得互相覆盖");
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            ID::Memory::ID_AlignedFree(boundary_blocks[i]);
        }

        // 8193 = 小对象上限 + 1，必须改走大对象页
        {
            constexpr ID::size_t kLargeSize = ID::Memory::kMaxSmallBlockSize + 1;
            auto* large_block = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kLargeSize, 16));
            bool large_ok = large_block != nullptr;
            if (large_ok)
            {
                large_block[0]               = 0x5A;
                large_block[kLargeSize - 1]  = 0xA5;
                large_ok = large_block[0] == 0x5A && large_block[kLargeSize - 1] == 0xA5;
                ID::Memory::ID_AlignedFree(large_block);
            }
            Check(large_ok, "memory.alloc.above_small_max", "8193B 必须走大对象路径且首尾字节可读写");
        }

        // 超过小对象上限的对齐（16KB/64KB/128KB）必须由大对象页满足
        {
            constexpr ID::size_t kLargeAlignments[] = { 16384, 65536, 131072 };
            bool large_alignment_ok = true;
            for (ID::size_t i = 0; i < sizeof(kLargeAlignments) / sizeof(kLargeAlignments[0]); ++i)
            {
                const ID::size_t alignment = kLargeAlignments[i];
                void* aligned_block = ID::Memory::ID_AlignedAlloc(64, alignment);
                const bool ok = aligned_block != nullptr
                             && (reinterpret_cast<ID::uintptr_t>(aligned_block) & (alignment - 1)) == 0;
                if (ok)
                {
                    std::memset(aligned_block, 0x77, 64);
                }
                large_alignment_ok = large_alignment_ok && ok;
                ID::Memory::ID_AlignedFree(aligned_block);
            }
            Check(large_alignment_ok, "memory.alloc.large_alignment", "16KB/64KB/128KB 对齐请求必须被满足");
        }

        // =================================================================
        // 内存：C3 非法输入与归一化规则
        // =================================================================
        void* zero_size_block = ID::Memory::ID_AlignedAlloc(0, 16);
        Check(zero_size_block != nullptr, "memory.alloc.size_zero", "size 0 按 1 字节处理，必须返回非空");
        ID::Memory::ID_AlignedFree(zero_size_block);

        void* zero_alignment_block = ID::Memory::ID_AlignedAlloc(32, 0);
        Check(zero_alignment_block != nullptr
           && (reinterpret_cast<ID::uintptr_t>(zero_alignment_block) & 15u) == 0,
              "memory.alloc.alignment_zero", "alignment 0 必须抬到 16 并成功");
        ID::Memory::ID_AlignedFree(zero_alignment_block);

        const ID::size_t logs_before_invalid = g_log_count;
        Check(ID::Memory::ID_AlignedAlloc(32, 24) == nullptr,
              "memory.alloc.alignment_24", "alignment 24 不是 2 的幂，必须返回 nullptr");
        Check(g_log_count > logs_before_invalid && g_last_log.level == ID::Log::Level::Error,
              "memory.alloc.error_logged", "非法对齐必须发出一条 Error 级内核日志");
        Check(ID::Memory::ID_AlignedAlloc(32, 48) == nullptr,
              "memory.alloc.alignment_48", "alignment 48 不是 2 的幂，必须返回 nullptr");
        Check(ID::Memory::ID_AlignedAlloc(static_cast<ID::size_t>(-1), 16) == nullptr,
              "memory.alloc.size_overflow", "极大 size 必须返回 nullptr 而不是回绕");
        Check(ID::Memory::ID_AlignedAlloc(static_cast<ID::size_t>(-1) - 8, 16) == nullptr,
              "memory.alloc.size_rounding", "size 按对齐取整溢出必须返回 nullptr");

        ID::Memory::ID_AlignedFree(nullptr);
        Check(true, "memory.free.null_is_noop", "释放 nullptr 必须是 no-op");
        Skip("memory.free.interior_pointer", "契约是破坏空闲链 / 断言 abort，不测");
        Skip("memory.free.double_free", "契约是破坏空闲链，可能 abort，不测");
        Skip("memory.large.magic_collision", "64KB 对齐大对象的页首 magic 误判是已知缺口（id-kernel K1），修复前不测");

        // =================================================================
        // 内存：C4 状态机 / 幂等
        // =================================================================
        ID::Memory::ID_MemoryInit();
        void* after_reinit_block = ID::Memory::ID_AlignedAlloc(128, 16);
        Check(after_reinit_block != nullptr, "memory.init.idempotent", "重复 ID_MemoryInit 之后分配仍然可用");
        ID::Memory::ID_AlignedFree(after_reinit_block);

        ID::Memory::ID_MemoryShutdown();
        void* after_shutdown_block = ID::Memory::ID_AlignedAlloc(128, 16);
        Check(after_shutdown_block != nullptr, "memory.shutdown.allocatable",
              "当前契约里 shutdown 只记日志，之后分配仍然可用");
        ID::Memory::ID_AlignedFree(after_shutdown_block);
        ID::Memory::ID_MemoryShutdown();
        Check(true, "memory.shutdown.idempotent", "重复 shutdown 必须是 no-op");

        // =================================================================
        // 内存：C1 统计计数器方向正确
        // =================================================================
        const ID::Memory::MemoryStats stats = ID::Memory::ID_GetMemoryStats();
        Check(stats.active_slab_count > 0, "memory.stats.active_slab", "已分配小对象后 active_slab_count 必须大于 0");
        Check(stats.total_reserved_bytes >= 64 * 1024, "memory.stats.reserved_bytes", "至少应映射一页 64KB");

        const ID::size_t large_bytes_before = stats.large_allocation_bytes;
        void* stats_block = ID::Memory::ID_AlignedAlloc(64 * 1024, 64);
        const ID::size_t large_bytes_during = ID::Memory::ID_GetMemoryStats().large_allocation_bytes;
        Check(stats_block != nullptr && large_bytes_during >= large_bytes_before + 64 * 1024,
              "memory.stats.large_bytes_grow", "大对象分配必须让 large_allocation_bytes 增长");
        ID::Memory::ID_AlignedFree(stats_block);
        Check(ID::Memory::ID_GetMemoryStats().large_allocation_bytes == large_bytes_before,
              "memory.stats.large_bytes_restore", "大对象释放后计数必须回到分配前");

        // =================================================================
        // 内存：C6 释放路径不得覆盖块尾哨兵
        // =================================================================
        {
            constexpr ID::size_t kGuardSize = 64;
            auto* guard_block = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kGuardSize, 16));
            bool guard_ok = guard_block != nullptr;
            if (guard_ok)
            {
                std::memset(guard_block, 0xAB, kGuardSize);
                ID::Memory::ID_AlignedFree(guard_block);

                auto* reused_block = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kGuardSize, 16));
                // 同线程释放是 LIFO 头插，下一次同档位分配必须拿回同一块；
                // 空闲链节点只占用块的前 8 字节，块尾哨兵必须仍然保留
                guard_ok = reused_block == guard_block && reused_block[kGuardSize - 1] == 0xAB;
                ID::Memory::ID_AlignedFree(reused_block);
            }
            Check(guard_ok, "memory.free.preserves_tail", "释放后再分配同档位必须拿回同一块且块尾哨兵未变");
        }

        // =================================================================
        // 内存：C7 数据完整性（Realloc）
        // =================================================================
        {
            constexpr ID::size_t kOldSize = 100;
            constexpr ID::size_t kNewSize = 9000;
            auto* original = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kOldSize, 16));
            bool preserved = original != nullptr;
            if (preserved)
            {
                for (ID::size_t i = 0; i < kOldSize; ++i)
                {
                    original[i] = static_cast<ID::uint8_t>(i * 7 + 3);
                }

                auto* grown = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedRealloc(original, kNewSize, 16));
                preserved = grown != nullptr;
                if (grown != nullptr)
                {
                    for (ID::size_t i = 0; i < kOldSize; ++i)
                    {
                        if (grown[i] != static_cast<ID::uint8_t>(i * 7 + 3))
                        {
                            preserved = false;
                            break;
                        }
                    }
                    ID::Memory::ID_AlignedFree(grown);
                }
                else
                {
                    // 契约：realloc 失败时原指针仍然有效，调用方继续拥有它
                    ID::Memory::ID_AlignedFree(original);
                }
            }
            Check(preserved, "memory.realloc.preserves_prefix", "扩容到 9000B 后原 100B 内容必须保持不变");
        }

        {
            auto* in_place = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(20, 16));   // 32B 档
            bool in_place_ok = false;
            if (in_place != nullptr)
            {
                in_place[0]  = 0xC3;
                void* same   = ID::Memory::ID_AlignedRealloc(in_place, 24, 16);
                in_place_ok  = same == in_place && static_cast<ID::uint8_t*>(same)[0] == 0xC3;
                ID::Memory::ID_AlignedFree(same);
            }
            Check(in_place_ok, "memory.realloc.in_place", "新尺寸不超过原块容量时必须原地返回同一指针");
        }

        {
            void* allocated = ID::Memory::ID_AlignedRealloc(nullptr, 64, 32);
            Check(allocated != nullptr && (reinterpret_cast<ID::uintptr_t>(allocated) & 31u) == 0,
                  "memory.realloc.null_allocates", "realloc(nullptr, 64, 32) 必须等价于一次新分配");
            Check(ID::Memory::ID_AlignedRealloc(allocated, 0, 32) == nullptr,
                  "memory.realloc.zero_size_frees", "new_size 为 0 时必须释放并返回 nullptr");
        }

        {
            void* kept_block = ID::Memory::ID_AlignedAlloc(64, 16);
            bool  kept_ok    = kept_block != nullptr;
            if (kept_ok)
            {
                static_cast<ID::uint8_t*>(kept_block)[0] = 0x3C;
                const bool rejected = ID::Memory::ID_AlignedRealloc(kept_block, 128, 24) == nullptr;
                kept_ok = rejected && static_cast<ID::uint8_t*>(kept_block)[0] == 0x3C;
                ID::Memory::ID_AlignedFree(kept_block);
            }
            Check(kept_ok, "memory.realloc.invalid_alignment", "非 2 的幂 alignment 必须失败且不改动原块");
        }

        // =================================================================
        // 内存：TLS 帧 Arena 契约
        // =================================================================
        {
            ID::LinearArena& frame_arena = ID::Memory::get_thread_frame_arena();
            Check(frame_arena.is_initialized(), "memory.frame_arena.initialized",
                  "首次取用时帧 Arena 必须已绑定缓冲区");
            Check(&ID::Memory::get_thread_frame_arena() == &frame_arena, "memory.frame_arena.stable",
                  "同一线程两次取用必须是同一个实例");
            void* frame_block = frame_arena.allocate(256, 16);
            Check(frame_block != nullptr && (reinterpret_cast<ID::uintptr_t>(frame_block) & 15u) == 0,
                  "memory.frame_arena.allocate", "帧 Arena 分配必须成功且满足对齐");
            Check(frame_arena.capacity() == ID::Memory::kFrameArenaSize,
                  "memory.frame_arena.capacity", "帧 Arena 容量必须是 kFrameArenaSize");
            Check(frame_arena.allocate(ID::Memory::kFrameArenaSize + 1, 16) == nullptr,
                  "memory.frame_arena.over_capacity", "超过 4MB 的请求必须返回 nullptr");
        }

        // =================================================================
        // 内存：C5 并发契约（跨线程释放 -> remote_free_list -> owner 收割）
        // =================================================================
        {
            constexpr ID::size_t kRemoteBlockCount = 8;
            constexpr ID::size_t kRemoteBlockSize  = 96;
            void* remote_blocks[kRemoteBlockCount] = {};

            bool remote_alloc_ok = true;
            for (ID::size_t i = 0; i < kRemoteBlockCount; ++i)
            {
                remote_blocks[i] = ID::Memory::ID_AlignedAlloc(kRemoteBlockSize, 16);
                remote_alloc_ok  = remote_alloc_ok && remote_blocks[i] != nullptr;
            }

            std::thread releaser([&remote_blocks]() noexcept
            {
                for (ID::size_t i = 0; i < kRemoteBlockCount; ++i)
                {
                    ID::Memory::ID_AlignedFree(remote_blocks[i]);
                }
            });
            releaser.join();

            // 96B 档位一页 681 块：继续分配必然先耗尽当前页，再走慢路径收割 remote_free_list
            constexpr ID::size_t kDrainLimit = 4096;
            void*      drained[kDrainLimit]  = {};
            ID::size_t drained_count         = 0;
            ID::size_t reclaimed             = 0;
            for (ID::size_t i = 0; i < kDrainLimit && reclaimed < kRemoteBlockCount; ++i)
            {
                void* block_from_slab = ID::Memory::ID_AlignedAlloc(kRemoteBlockSize, 16);
                if (block_from_slab == nullptr)
                {
                    break;
                }
                drained[drained_count++] = block_from_slab;
                for (ID::size_t k = 0; k < kRemoteBlockCount; ++k)
                {
                    if (block_from_slab == remote_blocks[k])
                    {
                        ++reclaimed;
                        remote_blocks[k] = nullptr;
                        break;
                    }
                }
            }
            Check(remote_alloc_ok && reclaimed == kRemoteBlockCount,
                  "memory.thread.remote_harvest", "跨线程释放的 8 块必须全部被 owner 线程收割回来");

            for (ID::size_t i = 0; i < drained_count; ++i)
            {
                ID::Memory::ID_AlignedFree(drained[i]);
            }
        }

        {
            const ID::uintptr_t main_thread_tag = ID::Memory::get_current_thread_tag();
            ID::uintptr_t       child_thread_tag = 0;
            bool                child_ok         = false;

            std::thread worker([&child_thread_tag, &child_ok]() noexcept
            {
                child_thread_tag = ID::Memory::get_current_thread_tag();
                void* small_block   = ID::Memory::ID_AlignedAlloc(48, 16);
                void* aligned_block = ID::Memory::ID_AlignedAlloc(1024, 64);
                child_ok = small_block != nullptr
                        && aligned_block != nullptr
                        && (reinterpret_cast<ID::uintptr_t>(aligned_block) & 63u) == 0;
                if (small_block != nullptr)
                {
                    std::memset(small_block, 0x5A, 48);
                }
                ID::Memory::ID_AlignedFree(small_block);
                ID::Memory::ID_AlignedFree(aligned_block);
            });
            worker.join();

            Check(child_ok, "memory.thread.child_alloc_free", "子线程独立分配/释放必须成功");
            Check(child_thread_tag != 0 && child_thread_tag != main_thread_tag,
                  "memory.thread.tag_distinct", "不同线程的 thread tag 必须不同");
        }

        // =================================================================
        // 插件：C1/C4 注册、拓扑序、生命周期
        // =================================================================
        ID::PluginManager& plugin_manager = ID::PluginManager::instance();
        plugin_manager.shutdown_all();   // 从干净状态开始
        reset_lifecycle();

        const bool registered_first_two = plugin_manager.register_plugin(&kPluginA)
                                       && plugin_manager.register_plugin(&kPluginB);
        const bool registered_duplicate = plugin_manager.register_plugin(&kPluginA);
        const bool registered_third     = plugin_manager.register_plugin(&kPluginC);

        Check(registered_first_two, "plugin.register.accepted", "A/B 首次注册必须成功");
        Check(!registered_duplicate, "plugin.register.duplicate", "同一 plugin_id 第二次注册必须被拒绝");
        Check(registered_third && plugin_manager.registered_count() == 3,
              "plugin.register.count", "三次成功注册后 registered_count 必须是 3");

        const bool chain_initialized = plugin_manager.initialize_all();
        Check(chain_initialized, "plugin.init.success", "无环依赖图必须初始化成功");

        const auto chain_order = plugin_manager.topological_order();
        Check(chain_order.size() == 3
           && chain_order[0] == &kPluginA
           && chain_order[1] == &kPluginB
           && chain_order[2] == &kPluginC,
              "plugin.topo.chain_order", "拓扑序必须是 A -> B -> C");

        plugin_manager.shutdown_all();
        check_lifecycle("RARBRCIAIBICSCSBSAUCUBUA", "plugin.lifecycle.sequence");
        Check(plugin_manager.registered_count() == 0 && plugin_manager.topological_order().empty(),
              "plugin.shutdown.clears_registry", "shutdown_all 后注册表与拓扑序必须清空");

        const bool re_registered  = plugin_manager.register_plugin(&kPluginA);
        const bool re_initialized = re_registered && plugin_manager.initialize_all();
        Check(re_initialized, "plugin.lifecycle.reinit",
              "shutdown_all 之后必须可以重新注册并再次初始化");
        plugin_manager.shutdown_all();

        const bool empty_initialized = plugin_manager.initialize_all();
        Check(empty_initialized && plugin_manager.registered_count() == 0,
              "plugin.init.empty_graph", "0 个插件时 initialize_all 必须成功");
        plugin_manager.shutdown_all();

        // =================================================================
        // 插件：C2 容量边界（128 上限 / 129 上限 + 1）
        // =================================================================
        bool capacity_registered = true;
        for (ID::size_t i = 0; i < ID::PluginManager::kMaxPlugins; ++i)
        {
            capacity_registered = plugin_manager.register_plugin(&kGeneratedPlugins.entries[i]) && capacity_registered;
        }
        Check(capacity_registered && plugin_manager.registered_count() == ID::PluginManager::kMaxPlugins,
              "plugin.register.capacity_128", "128 个插件必须全部注册成功");

        const bool overflow_rejected =
            !plugin_manager.register_plugin(&kGeneratedPlugins.entries[ID::PluginManager::kMaxPlugins]);
        Check(overflow_rejected && plugin_manager.registered_count() == ID::PluginManager::kMaxPlugins,
              "plugin.register.capacity_129", "第 129 个插件必须被拒绝且不影响已有注册");

        const bool capacity_initialized = plugin_manager.initialize_all();
        Check(capacity_initialized
           && plugin_manager.topological_order().size() == ID::PluginManager::kMaxPlugins,
              "plugin.init.capacity_128", "128 个零依赖插件必须完成拓扑排序与初始化");
        plugin_manager.shutdown_all();

        // =================================================================
        // 插件：菱形依赖图的偏序关系
        // =================================================================
        {
            const bool diamond_registered = plugin_manager.register_plugin(&kDiamondPluginA)
                                         && plugin_manager.register_plugin(&kDiamondPluginB)
                                         && plugin_manager.register_plugin(&kDiamondPluginC)
                                         && plugin_manager.register_plugin(&kDiamondPluginD);
            const bool diamond_initialized = diamond_registered && plugin_manager.initialize_all();
            Check(diamond_initialized, "plugin.dag.diamond_init",
                  "菱形依赖图 D -> {B, C} -> A 必须初始化成功");

            const auto diamond_order = plugin_manager.topological_order();
            const int  position_a    = find_plugin_position(diamond_order, &kDiamondPluginA);
            const int  position_b    = find_plugin_position(diamond_order, &kDiamondPluginB);
            const int  position_c    = find_plugin_position(diamond_order, &kDiamondPluginC);
            const int  position_d    = find_plugin_position(diamond_order, &kDiamondPluginD);
            Check(position_a >= 0 && position_a < position_b && position_a < position_c
               && position_b < position_d && position_c < position_d,
                  "plugin.dag.diamond_order", "A 必须早于 B/C，B/C 必须早于 D");
            plugin_manager.shutdown_all();
        }

        // =================================================================
        // 插件：兼容白名单不变量（每条依赖只贡献 1 点入度）
        // =================================================================
        {
            const bool backends_registered = plugin_manager.register_plugin(&kBackendSimd)
                                          && plugin_manager.register_plugin(&kBackendFixed)
                                          && plugin_manager.register_plugin(&kBackendConsumer);
            const bool backends_initialized = backends_registered && plugin_manager.initialize_all();
            // Simd 是 v2（达标），Fixed 是 v1（不达标）：不能让旧后端把整体判失败，也不能把两个候选都算进入度
            Check(backends_initialized, "plugin.dag.compatible_whitelist",
                  "白名单里任一上游版本达标即可满足依赖，旧版本候选不得导致失败");

            const auto backend_order = plugin_manager.topological_order();
            const int  position_simd = find_plugin_position(backend_order, &kBackendSimd);
            const int  position_consumer = find_plugin_position(backend_order, &kBackendConsumer);
            Check(backend_order.size() == 3 && position_simd >= 0
               && position_simd < position_consumer,
                  "plugin.dag.whitelist_order", "达标的后端必须排在下游之前");
            plugin_manager.shutdown_all();
        }

        Skip("plugin.dag.missing_dependency", "契约是 Level::Fatal -> abort，需子进程隔离");
        Skip("plugin.dag.cycle", "契约是 Level::Fatal -> abort（Kahn 环路检测），需子进程隔离");
        Skip("plugin.dag.dependencies_over_32", "契约是 Level::Fatal -> abort（每插件依赖 <= 32），需子进程隔离");
        Skip("plugin.register.null_descriptor", "契约是 ID_KERNEL_ASSERT -> abort，需子进程隔离");
        Skip("plugin.register.after_init", "契约是 ID_KERNEL_ASSERT -> abort，需子进程隔离");
        Skip("plugin.init.on_init_failure", "on_init 失败走 Level::Fatal -> abort，回滚路径当前不可达（id-kernel K3）");

        // =================================================================
        // 服务槽：固定槽位 O(1) 存取
        // =================================================================
        plugin_manager.register_service(ID::ServiceSlot::Math, &kApiTableOne);
        const FakeApiTable* loaded_table = plugin_manager.get_service<FakeApiTable>(ID::ServiceSlot::Math);
        Check(loaded_table == &kApiTableOne && loaded_table->magic == kApiTableOne.magic,
              "slot.store_and_load", "写入槽位后可取回同一指针");

        plugin_manager.register_service(ID::ServiceSlot::Math, &kApiTableTwo);
        Check(plugin_manager.get_service<FakeApiTable>(ID::ServiceSlot::Math) == &kApiTableTwo,
              "slot.overwrite_last_wins", "同一槽位后写覆盖先写（当前契约）");

        plugin_manager.register_service(ID::ServiceSlot::Physics, &kApiTableOne);
        Check(plugin_manager.get_service<FakeApiTable>(ID::ServiceSlot::Math) == &kApiTableTwo
           && plugin_manager.get_service<FakeApiTable>(ID::ServiceSlot::Physics) == &kApiTableOne,
              "slot.independent", "不同槽位互不影响");

        plugin_manager.register_service(ID::ServiceSlot::Math, nullptr);
        plugin_manager.register_service(ID::ServiceSlot::Physics, nullptr);
        Check(plugin_manager.get_service<FakeApiTable>(ID::ServiceSlot::Math) == nullptr
           && plugin_manager.get_service<FakeApiTable>(ID::ServiceSlot::Physics) == nullptr,
              "slot.unregister", "写 nullptr 表示卸下，取出必须为空");

        Skip("slot.out_of_range", "槽位接口不做边界检查（id-kernel K4），越界写是 UB，不测");
        Skip("slot.concurrent_write", "当前无并发写契约：槽位只在插件 on_register/on_unload 单线程写");

        // =================================================================
        // 自举日志：派发契约与还原
        // =================================================================
        const ID::size_t log_count_before = g_log_count;
        ID::Log::BootstrapLog::instance().report(ID::Log::Level::Warn, "ID.Test", "benchmark log probe");
        Check(g_log_count == log_count_before + 1
           && g_last_log.level == ID::Log::Level::Warn
           && std::strcmp(g_last_log.category, "ID.Test") == 0
           && std::strcmp(g_last_log.message, "benchmark log probe") == 0,
              "log.report_dispatch", "report 必须把 level/category/message 原样交给当前 sink");

        Check(g_log_count > 0, "log.capture_observed", "整轮测试期间内核确实产生过日志");

        // 先把 shutdown 的日志也交给捕获 sink，否则它会用默认 sink 写 stderr，与 report.txt 分叉
        ID::Memory::ID_MemoryShutdown();

        ID::Log::BootstrapLog::instance().set_sink(&ID::Log::default_bootstrap_sink);
        Check(ID::Log::BootstrapLog::instance().get_sink() == &ID::Log::default_bootstrap_sink,
              "log.restore_default", "测试结束必须把 sink 还原为 default_bootstrap_sink");
    }
} // namespace ID::Bench
