#include "Kernel/Memory.hpp"
#include "Kernel/BootstrapLog.hpp"

#include <atomic>
#include <cstring>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#else
    #include <sys/mman.h>
#endif

namespace ID::Memory
{
    namespace
    {
        inline constexpr uint32_t kLargeMagic = 0x49444C47u; // 'IDLG' 的 16 进制表示，用于 >8KB 大对象的魔数校验

        // 大对象 (> 8KB) 头部元数据，放置在 64KB 对齐块的起始位置，使 ptr & kSpanMask 同样能 O(1) 定位到它
        struct alignas(kCacheLineSize) LargeBlockHeader
        {
            uint32_t    magic                   = 0;        // 固定为 kLargeMagic ('IDLG')
            uint32_t    offset_to_user_ptr      = 0;        // 从 64KB 块首地址到用户数据指针的偏移量
            size_t      total_allocated_bytes   = 0;        // 本次向 OS 申请的总字节数
            void*       raw_base_address        = nullptr;  // OS 原始返回基地址
        };

        // 全局原子统计数据（constinit 编译期零初始化）
        constinit std::atomic<size_t> g_total_reserved_bytes{ 0 };
        constinit std::atomic<size_t> g_active_slab_count{ 0 };
        constinit std::atomic<size_t> g_large_allocation_bytes{ 0 };

        // 线程局部唯一标识锚点变量
        thread_local uint8_t s_tls_thread_anchor = 0;

        // 根据对齐后的请求字节数查找对应的 Size Class 档位下标 (0 ~ 14)
        ID_FORCEINLINE uint32_t select_size_class(size_t aligned_size) noexcept
        {
            for (uint32_t i = 0; i < kNumSizeClasses; ++i)
            {
                if (kSizeClasses[i] >= aligned_size)
                {
                    return i;
                }
            }
            return static_cast<uint32_t>(kNumSizeClasses);
        }

        // FrameArena 空间不足时的回调函数
        void on_frame_arena_exhausted(size_t, size_t) noexcept
        {
            ID_KERNEL_LOG(Log::Level::Warn, "TLS LinearArena buffer exhausted! Returning nullptr.");
        }

        // ---------------------------------------------------------------------
        // 15 档 SlabSpan 线程局部调度池 (Thread-Local Heap)
        // ---------------------------------------------------------------------
        class ThreadSlabScheduler
        {
        public:
            constexpr ThreadSlabScheduler() noexcept = default;

            // 快路径：直接命中当前线程对应档位的活跃 SlabSpan
            ID_FORCEINLINE void* allocate_small(uint32_t class_idx) noexcept
            {
                if (SlabSpan* slab = m_active_slabs[class_idx]; slab != nullptr) [[likely]]
                {
                    if (void* block = slab->allocate_local(); block != nullptr) [[likely]]
                    {
                        return block;
                    }
                }
                return allocate_small_slow(class_idx);
            }

            // 慢路径：遍历同档位 Slab 链表、收割跨线程 remote_free_list，或向 OS 申请新 64KB SlabSpan
            ID_NOINLINE void* allocate_small_slow(uint32_t class_idx) noexcept
            {
                // 1. 扫描当前线程已持有的同档位 SlabSpan 链表，尝试复用或收割跨线程归还块
                for (SlabSpan* slab = m_active_slabs[class_idx]; slab != nullptr; slab = slab->next_slab)
                {
                    if (void* block = slab->allocate_local(); block != nullptr)
                    {
                        m_active_slabs[class_idx] = slab;
                        return block;
                    }
                    if (void* block = slab->harvest_remote(); block != nullptr)
                    {
                        m_active_slabs[class_idx] = slab;
                        return block;
                    }
                }

                // 2. 现有 Slab 全部满载，向 OS 申请一块新的 64KB 对齐虚拟内存并格式化为 SlabSpan
                void* raw_span = os_alloc_aligned_64k(SlabSpan::kSpanSize);
                if (!raw_span) [[unlikely]]
                {
                    ID_KERNEL_LOG(Log::Level::Fatal, "Failed to allocate 64KB virtual memory page for SlabSpan!");
                    return nullptr;
                }

                SlabSpan* new_slab = SlabSpan::format(raw_span, class_idx, kSizeClasses[class_idx], get_current_thread_tag());
                new_slab->next_slab       = m_active_slabs[class_idx];
                m_active_slabs[class_idx] = new_slab;
                g_active_slab_count.fetch_add(1, std::memory_order_relaxed);

                return new_slab->allocate_local();
            }

            ID_FORCEINLINE LinearArena& frame_arena() noexcept
            {
                if (!m_frame_arena.is_initialized()) [[unlikely]]
                {
                    init_frame_arena();
                }
                return m_frame_arena;
            }

            ID_NOINLINE void init_frame_arena() noexcept
            {
                if (!m_frame_arena.is_initialized())
                {
                    if (void* buf = os_alloc_aligned_64k(kFrameArenaSize))
                    {
                        m_frame_arena.init(buf, kFrameArenaSize, &on_frame_arena_exhausted);
                    }
                }
            }

        private:
            SlabSpan*   m_active_slabs[kNumSizeClasses] = { nullptr };  // 15 档小对象当前活跃的 SlabSpan 指针数组
            LinearArena m_frame_arena{};                                // 当前线程私有的 4MB 帧线性分配器
        };

        thread_local ThreadSlabScheduler g_tls_scheduler{};

        // 大对象 (> 8KB) 分配实现
        ID_NOINLINE void* allocate_large(size_t size, size_t alignment) noexcept
        {
            size_t effective_align = alignment < kCacheLineSize ? kCacheLineSize : alignment;
            size_t total_needed    = sizeof(LargeBlockHeader) + effective_align + size;
            size_t alloc_bytes     = (total_needed + (SlabSpan::kSpanSize - 1)) & ~(SlabSpan::kSpanSize - 1);

            void* raw_base = os_alloc_aligned_64k(alloc_bytes);
            if (!raw_base) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Fatal, "Failed to allocate large virtual memory block!");
                return nullptr;
            }

            uintptr_t base_addr = reinterpret_cast<uintptr_t>(raw_base);
            uintptr_t user_addr = (base_addr + sizeof(LargeBlockHeader) + (effective_align - 1)) & ~(effective_align - 1);

            auto* header = reinterpret_cast<LargeBlockHeader*>(user_addr & SlabSpan::kSpanMask);
            header->magic                 = kLargeMagic;
            header->offset_to_user_ptr    = static_cast<uint32_t>(user_addr - reinterpret_cast<uintptr_t>(header));
            header->total_allocated_bytes = alloc_bytes;
            header->raw_base_address      = raw_base;

            g_large_allocation_bytes.fetch_add(alloc_bytes, std::memory_order_relaxed);
            return reinterpret_cast<void*>(user_addr);
        }
    } // namespace

    // -------------------------------------------------------------------------
    // OS 64KB 对齐虚拟内存页管理实现
    // -------------------------------------------------------------------------
    void* os_alloc_aligned_64k(size_t size_bytes) noexcept
    {
        size_t aligned_size = (size_bytes + (SlabSpan::kSpanSize - 1)) & ~(SlabSpan::kSpanSize - 1);

        #if defined(_WIN32)
            // Windows VirtualAlloc 的分配粒度 (dwAllocationGranularity) 天然保证 64KB 边界对齐
            void* ptr = VirtualAlloc(nullptr, aligned_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            ID_KERNEL_ASSERT((reinterpret_cast<uintptr_t>(ptr) & (SlabSpan::kSpanSize - 1)) == 0, "VirtualAlloc must be 64KB aligned");
        #else
            void* ptr = nullptr;
            void* raw = mmap(nullptr, aligned_size + SlabSpan::kSpanSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (raw != MAP_FAILED)
            {
                uintptr_t aligned_addr = (reinterpret_cast<uintptr_t>(raw) + (SlabSpan::kSpanSize - 1)) & SlabSpan::kSpanMask;
                ptr = reinterpret_cast<void*>(aligned_addr);
            }
        #endif

        if (ptr)
        {
            g_total_reserved_bytes.fetch_add(aligned_size, std::memory_order_relaxed);
        }
        return ptr;
    }

    void os_free_aligned_64k(void* base_ptr, size_t size_bytes) noexcept
    {
        if (!base_ptr)
        {
            return;
        }
        size_t aligned_size = (size_bytes + (SlabSpan::kSpanSize - 1)) & ~(SlabSpan::kSpanSize - 1);

        #if defined(_WIN32)
            VirtualFree(base_ptr, 0, MEM_RELEASE);
        #else
            munmap(base_ptr, aligned_size);
        #endif

        g_total_reserved_bytes.fetch_sub(aligned_size, std::memory_order_relaxed);
    }

    uintptr_t get_current_thread_tag() noexcept
    {
        return reinterpret_cast<uintptr_t>(&s_tls_thread_anchor);
    }

    LinearArena& get_thread_frame_arena() noexcept
    {
        return g_tls_scheduler.frame_arena();
    }

    // -------------------------------------------------------------------------
    // 全局统一 C ABI 内存分配接口实现
    // -------------------------------------------------------------------------
    extern "C"
    {
        void ID_MemoryInit() noexcept
        {
            g_tls_scheduler.init_frame_arena();
            ID_KERNEL_LOG(Log::Level::Info, "ID.Kernel lock-free Slab/Page memory subsystem initialized.");
        }

        void ID_MemoryShutdown() noexcept
        {
            ID_KERNEL_LOG(Log::Level::Info, "ID.Kernel lock-free Slab/Page memory subsystem shut down.");
        }

        void* ID_AlignedAlloc(size_t size, size_t alignment) noexcept
        {
            if (size == 0) [[unlikely]]
            {
                size = 1;
            }
            if (alignment < 16)
            {
                alignment = 16; // 默认至少保证 16 字节 (SSE) 对齐
            }

            size_t aligned_req = (size + (alignment - 1)) & ~(alignment - 1);
            if (aligned_req <= kMaxSmallBlockSize && alignment <= kMaxSmallBlockSize) [[likely]]
            {
                uint32_t class_idx = select_size_class(aligned_req);
                return g_tls_scheduler.allocate_small(class_idx);
            }

            return allocate_large(size, alignment);
        }

        void ID_AlignedFree(void* ptr) noexcept
        {
            if (!ptr) [[unlikely]]
            {
                return;
            }

            // O(1) 掩码寻址：仅 1 条按位与指令定位所属 64KB 块的头部
            SlabSpan* slab = SlabSpan::from_ptr(ptr);
            if (slab->magic == SlabSpan::kSlabMagic) [[likely]]
            {
                slab->deallocate(ptr, get_current_thread_tag());
                return;
            }

            auto* large_hdr = reinterpret_cast<LargeBlockHeader*>(slab);
            if (large_hdr->magic == kLargeMagic)
            {
                size_t bytes   = large_hdr->total_allocated_bytes;
                void* raw_base = large_hdr->raw_base_address;
                g_large_allocation_bytes.fetch_sub(bytes, std::memory_order_relaxed);
                os_free_aligned_64k(raw_base, bytes);
                return;
            }

            ID_KERNEL_ASSERT(false, "ID_AlignedFree detected corrupted memory block or invalid pointer!");
        }

        void* ID_AlignedRealloc(void* ptr, size_t new_size, size_t alignment) noexcept
        {
            if (!ptr)
            {
                return ID_AlignedAlloc(new_size, alignment);
            }
            if (new_size == 0)
            {
                ID_AlignedFree(ptr);
                return nullptr;
            }

            uintptr_t addr      = reinterpret_cast<uintptr_t>(ptr);
            SlabSpan* slab      = SlabSpan::from_ptr(ptr);
            size_t old_capacity = 0;

            if (slab->magic == SlabSpan::kSlabMagic)
            {
                old_capacity = slab->block_size;
                if (new_size <= old_capacity && (addr & (alignment - 1)) == 0)
                {
                    return ptr; // 原块容量足够，零拷贝原地复用
                }
            }
            else
            {
                auto* large_hdr = reinterpret_cast<LargeBlockHeader*>(slab);
                if (large_hdr->magic == kLargeMagic)
                {
                    old_capacity = large_hdr->total_allocated_bytes - large_hdr->offset_to_user_ptr;
                }
            }

            void* new_ptr = ID_AlignedAlloc(new_size, alignment);
            if (new_ptr && old_capacity > 0)
            {
                std::memcpy(new_ptr, ptr, old_capacity < new_size ? old_capacity : new_size);
                ID_AlignedFree(ptr);
            }
            return new_ptr;
        }

        MemoryStats ID_GetMemoryStats() noexcept
        {
            return MemoryStats
            {
                g_total_reserved_bytes.load(std::memory_order_relaxed),
                g_active_slab_count.load(std::memory_order_relaxed),
                g_large_allocation_bytes.load(std::memory_order_relaxed)
            };
        }
    } // extern "C"
} // namespace ID::Memory
