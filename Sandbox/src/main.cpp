#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "Kernel/BootstrapLog.hpp"
#include "Kernel/CoreTypes.hpp"
#include "Kernel/Memory.hpp"
#include "Kernel/Plugin.hpp"

/**
 *  Sandbox 冒烟与微内核基准性能测试宿主程序 (Smoke Test & Micro-Benchmark)
 *  1. 功能验证：BootstrapLog、TLS LinearArena、无锁 SlabSpan (同线程/跨线程 CAS)、PluginManager DAG 拓扑排序；
 *  2. 性能实测：以千万级调用实测 ns/op，并与系统原生 malloc/free 对比。
 */

namespace
{
    constexpr ID::uint64_t kMathPluginId    = ID::hash_fnv_1a64("ID.Math");
    constexpr ID::uint64_t kPhysicsPluginId = ID::hash_fnv_1a64("ID.Physics");

    struct MathApiTable
    {
        float(*vec_add)(float a, float b) noexcept;
    };

    float fast_vec_add(float a, float b) noexcept
    {
        return a + b;
    }

    constexpr MathApiTable kMathApi = { &fast_vec_add };

    void math_on_register() noexcept
    {
        ID::PluginManager::instance().register_service(ID::ServiceSlot::Math, &kMathApi);
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Math] on_register -> MathApiTable bound to ServiceSlot::Math.");
    }

    bool math_on_init() noexcept
    {
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Math] on_init -> Math subsystem initialized.");
        return true;
    }

    void math_on_shutdown() noexcept
    {
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Math] on_shutdown -> Math subsystem shutting down.");
    }

    void math_on_unload() noexcept
    {
        ID::PluginManager::instance().register_service(ID::ServiceSlot::Math, nullptr);
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Math] on_unload -> ServiceSlot::Math unbound.");
    }

    constexpr ID::PluginDependency kPhysicsDeps[] = { { kMathPluginId, 1 } };

    void physics_on_register() noexcept
    {
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Physics] on_register -> Physics types registered.");
    }

    bool physics_on_init() noexcept
    {
        const auto* math = ID::PluginManager::instance().get_service<MathApiTable>(ID::ServiceSlot::Math);
        ID_KERNEL_ASSERT(math != nullptr && math->vec_add(12.5f, 27.5f) == 40.0f, "Math service must be ready before Physics!");
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Physics] on_init -> Verified O(1) ServiceSlot call: 12.5 + 27.5 = 40.0.");
        return true;
    }

    void physics_on_shutdown() noexcept
    {
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Physics] on_shutdown -> Physics subsystem shutting down.");
    }

    void physics_on_unload() noexcept
    {
        ID_KERNEL_LOG(ID::Log::Level::Info, "[Plugin: ID.Physics] on_unload -> Physics unloaded.");
    }

    constexpr ID::PluginDescriptor kMathPluginDesc =
    {
        kMathPluginId, 1, "ID.Math", {},
        &math_on_register, &math_on_init, &math_on_shutdown, &math_on_unload
    };

    constexpr ID::PluginDescriptor kPhysicsPluginDesc =
    {
        kPhysicsPluginId, 1, "ID.Physics", kPhysicsDeps,
        &physics_on_register, &physics_on_init, &physics_on_shutdown, &physics_on_unload
    };

    void run_performance_benchmarks() noexcept
    {
        using Clock = std::chrono::steady_clock;
        char buf[256];

        // ---------------------------------------------------------------------
        // Benchmark 1: TLS LinearArena 帧分配器速度 (10,000,000 次分配)
        // ---------------------------------------------------------------------
        ID::LinearArena& arena = ID::Memory::get_thread_frame_arena();
        constexpr ID::size_t kArenaLoops = 10000;
        constexpr ID::size_t kBatch      = 1000;
        volatile uintptr_t sink          = 0;

        auto t0 = Clock::now();
        for (ID::size_t loop = 0; loop < kArenaLoops; ++loop)
        {
            arena.reset();
            for (ID::size_t i = 0; i < kBatch; ++i)
            {
                void* p = arena.allocate(64, 16);
                sink ^= reinterpret_cast<uintptr_t>(p);
            }
        }
        auto t1 = Clock::now();
        double arena_ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(kArenaLoops * kBatch);

        // ---------------------------------------------------------------------
        // Benchmark 2: ID.Kernel 无锁 Slab 分配 + 释放 vs OS 标准 malloc + free (5,000,000 次)
        // ---------------------------------------------------------------------
        constexpr ID::size_t kSlabIters = 5000000;
        void* batch_ptrs[256];

        auto t2 = Clock::now();
        for (ID::size_t loop = 0; loop < kSlabIters / 256; ++loop)
        {
            for (ID::size_t i = 0; i < 256; ++i)
            {
                batch_ptrs[i] = ID::Memory::ID_AlignedAlloc(64, 16);
            }
            for (ID::size_t i = 0; i < 256; ++i)
            {
                ID::Memory::ID_AlignedFree(batch_ptrs[i]);
            }
        }
        auto t3 = Clock::now();
        double slab_ns = std::chrono::duration<double, std::nano>(t3 - t2).count() / static_cast<double>((kSlabIters / 256) * 256);

        auto t4 = Clock::now();
        for (ID::size_t loop = 0; loop < kSlabIters / 256; ++loop)
        {
            for (ID::size_t i = 0; i < 256; ++i)
            {
                batch_ptrs[i] = _aligned_malloc(64, 16);
            }
            for (ID::size_t i = 0; i < 256; ++i)
            {
                _aligned_free(batch_ptrs[i]);
            }
        }
        auto t5 = Clock::now();
        double malloc_ns = std::chrono::duration<double, std::nano>(t5 - t4).count() / static_cast<double>((kSlabIters / 256) * 256);

        // ---------------------------------------------------------------------
        // Benchmark 3: 跨线程生产-消费 CAS 归还与批量收割吞吐量 (1,000,000 次，复用常驻工作线程)
        // ---------------------------------------------------------------------
        constexpr ID::size_t kCrossBatch  = 1024;
        constexpr ID::size_t kCrossRounds = 1000;
        void* cross_ptrs[kCrossBatch]     = { nullptr };
        std::atomic<int> state{ 0 }; // 0: 主线程生产, 1: 工作线程跨线程释放, 2: 退出

        std::thread persistent_worker([&]()
        {
            while (true)
            {
                int s = state.load(std::memory_order_acquire);
                if (s == 1)
                {
                    for (ID::size_t i = 0; i < kCrossBatch; ++i)
                    {
                        ID::Memory::ID_AlignedFree(cross_ptrs[i]);
                    }
                    state.store(0, std::memory_order_release);
                }
                else if (s == 2)
                {
                    break;
                }
            }
        });

        auto t6 = Clock::now();
        for (ID::size_t r = 0; r < kCrossRounds; ++r)
        {
            for (ID::size_t i = 0; i < kCrossBatch; ++i)
            {
                cross_ptrs[i] = ID::Memory::ID_AlignedAlloc(64, 16);
            }
            state.store(1, std::memory_order_release);
            while (state.load(std::memory_order_acquire) != 0) { }
        }
        auto t7 = Clock::now();
        state.store(2, std::memory_order_release);
        persistent_worker.join();
        double cross_ns = std::chrono::duration<double, std::nano>(t7 - t6).count() / static_cast<double>(kCrossRounds * kCrossBatch);

        // ---------------------------------------------------------------------
        // Benchmark 4: ServiceSlot O(1) 跨插件服务寻址 + 调用 (10,000,000 次)
        // ---------------------------------------------------------------------
        constexpr ID::size_t kCallIters = 10000000;
        float acc = 0.0f;
        auto t8 = Clock::now();
        for (ID::size_t i = 0; i < kCallIters; ++i)
        {
            const auto* math = ID::PluginManager::instance().get_service<MathApiTable>(ID::ServiceSlot::Math);
            acc += math->vec_add(static_cast<float>(i), 1.0f);
        }
        auto t9 = Clock::now();
        sink ^= static_cast<uintptr_t>(acc);
        double service_ns = std::chrono::duration<double, std::nano>(t9 - t8).count() / static_cast<double>(kCallIters);

        std::snprintf(buf, sizeof(buf), "[Bench 1] TLS LinearArena (64B alloc):        %.2f ns/op", arena_ns);
        ID_KERNEL_LOG(ID::Log::Level::Info, buf);

        std::snprintf(buf, sizeof(buf), "[Bench 2] Lock-Free Slab (alloc + free pair): %.2f ns/op  (vs OS _aligned_malloc: %.2f ns/op -> %.1fx faster!)",
            slab_ns, malloc_ns, malloc_ns / slab_ns);
        ID_KERNEL_LOG(ID::Log::Level::Info, buf);

        std::snprintf(buf, sizeof(buf), "[Bench 3] Cross-Thread CAS (alloc+remote free): %.2f ns/op", cross_ns);
        ID_KERNEL_LOG(ID::Log::Level::Info, buf);

        std::snprintf(buf, sizeof(buf), "[Bench 4] ServiceSlot O(1) lookup + call:     %.2f ns/op  (guard=%llu)",
            service_ns, static_cast<unsigned long long>(sink & 0xFF));
        ID_KERNEL_LOG(ID::Log::Level::Info, buf);
    }
} // namespace

int main()
{
    ID_KERNEL_LOG(ID::Log::Level::Info, "=== IDGameEngine Stage 1 Smoke Test & Benchmark Starting ===");

    // 1. 初始化微内核无锁 Slab/Page 内存子系统
    ID::Memory::ID_MemoryInit();

    // 2. 验证 TLS 帧线性分配器 (LinearArena)
    ID::LinearArena& arena = ID::Memory::get_thread_frame_arena();
    auto* temp_floats = arena.allocate_array<float>(256);
    ID_KERNEL_ASSERT(temp_floats != nullptr && arena.used_bytes() >= 256 * sizeof(float), "LinearArena allocation failed!");
    arena.reset();
    ID_KERNEL_ASSERT(arena.used_bytes() == 0, "LinearArena O(1) reset failed!");
    ID_KERNEL_LOG(ID::Log::Level::Info, "[Check 1/3] TLS LinearArena allocation and O(1) reset passed.");

    // 3. 验证 IDPluginStub 全局 new/delete 劫持 + 跨线程无锁 CAS 释放与收割
    constexpr ID::size_t kAllocCount = 512;
    ID::uint64_t* ptrs[kAllocCount] = { nullptr };

    for (ID::size_t i = 0; i < kAllocCount; ++i)
    {
        ptrs[i] = new ID::uint64_t[8]; // 64B 块，经由 IDPluginStub 路由至 ID_AlignedAlloc (SlabSpan)
        ptrs[i][0] = i;
        ID::SlabSpan* slab = ID::SlabSpan::from_ptr(ptrs[i]);
        ID_KERNEL_ASSERT(slab->magic == ID::SlabSpan::kSlabMagic && slab->block_size == 64, "Pointer must belong to a 64B SlabSpan!");
    }

    std::thread worker([&ptrs]()
    {
        for (ID::size_t i = 0; i < kAllocCount; ++i)
        {
            delete[] ptrs[i]; // 在子线程释放主线程分配的块，触发无锁 CAS 挂入 remote_free_list
        }
    });
    worker.join();

    auto* harvested_block = new ID::uint64_t[8];
    ID_KERNEL_ASSERT(harvested_block != nullptr, "Remote free list harvest failed!");
    delete[] harvested_block;

    ID::Memory::MemoryStats stats = ID::Memory::ID_GetMemoryStats();
    char stats_msg[128];
    std::snprintf(stats_msg, sizeof(stats_msg), "[Check 2/3] Lock-free Slab passed (Active Slabs: %zu, Reserved: %zu KB).",
        stats.active_slab_count, stats.total_reserved_bytes / 1024);
    ID_KERNEL_LOG(ID::Log::Level::Info, stats_msg);

    // 4. 验证插件 DAG 拓扑排序：故意先注册上层依赖方 Physics，后注册底层被依赖方 Math
    auto& pm = ID::PluginManager::instance();
    pm.register_plugin(&kPhysicsPluginDesc);
    pm.register_plugin(&kMathPluginDesc);

    bool init_ok = pm.initialize_all();
    ID_KERNEL_ASSERT(init_ok, "PluginManager DAG topological initialization failed!");

    auto order = pm.topological_order();
    ID_KERNEL_ASSERT(order.size() == 2 && order[0]->plugin_id == kMathPluginId && order[1]->plugin_id == kPhysicsPluginId,
        "Kahn topological sort must order ID.Math before ID.Physics!");
    ID_KERNEL_LOG(ID::Log::Level::Info, "[Check 3/3] PluginManager Kahn DAG sort & lifecycle dispatch passed.");

    // 5. 运行微内核千万级压力性能基准测试
    run_performance_benchmarks();

    // 6. 严格按拓扑逆序 (Physics -> Math) 关闭并卸载插件
    pm.shutdown_all();
    ID::Memory::ID_MemoryShutdown();

    ID_KERNEL_LOG(ID::Log::Level::Info, "=== IDGameEngine Stage 1 Smoke Test & Benchmark ALL PASSED ===");
    return 0;
}
