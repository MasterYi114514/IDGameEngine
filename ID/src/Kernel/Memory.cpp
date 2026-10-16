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

        // 大对象头。和用户指针在同一页时放在该页页首；用户指针本身 64KB 对齐时放在前一页，避免盖住用户缓冲区
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

            /**
             *  @brief 本线程小对象快路径，只查当前档位缓存的那一页
             *  @param class_idx Size Class 下标
             *  @return 块指针；缓存页没有空闲块时进入慢路径
             */
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

            /**
             *  @brief 从档位链表头重扫空闲块，扫不到才向 OS 要新的 64KB 页
             *  @param class_idx Size Class 下标
             *  @return 块指针；虚拟内存耗尽时返回 nullptr
             *  @note m_slab_heads 是链表头，只在新页前插时改。m_active_slabs 只是快路径缓存，可以指到链表中部。把缓存当成链表头会丢掉前面的页，那些页上的释放再也分配不回来
             */
            ID_NOINLINE void* allocate_small_slow(uint32_t class_idx) noexcept
            {
                for (SlabSpan* slab = m_slab_heads[class_idx]; slab != nullptr; slab = slab->next_slab)
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

                void* raw_span = os_alloc_aligned_64k(SlabSpan::kSpanSize);
                if (!raw_span) [[unlikely]]
                {
                    ID_KERNEL_LOG(Log::Level::Fatal, "Failed to allocate 64KB virtual memory page for SlabSpan!");
                    return nullptr;
                }

                SlabSpan* new_slab = SlabSpan::format(raw_span, class_idx, kSizeClasses[class_idx], get_current_thread_tag());
                new_slab->next_slab       = m_slab_heads[class_idx];
                m_slab_heads[class_idx]   = new_slab;
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
            SlabSpan*   m_slab_heads[kNumSizeClasses]   = { nullptr };  // 档位链表头，只在新建 Span 时前插
            SlabSpan*   m_active_slabs[kNumSizeClasses] = { nullptr };  // 快路径缓存，可以指向链表中部
            LinearArena m_frame_arena{};                                // 当前线程私有的 4MB 帧线性分配器
        };

        thread_local ThreadSlabScheduler g_tls_scheduler{};

        /**
         *  @brief 把调用方传入的对齐修正成不小于 16 的 2 的幂
         *  @param alignment 入参会被就地抬高到至少 16
         *  @return 修正后是 2 的幂时返回 true。24 这类值返回 false，调用方必须放弃本次分配
         */
        bool prepare_alignment(size_t& alignment) noexcept
        {
            if (alignment < 16)
            {
                alignment = 16;
            }
            return (alignment & (alignment - 1)) == 0;
        }

        /**
         *  @brief 分配大于 8KB 的对象，把头放在用户缓冲区之外
         *  @param size 用户请求的字节数
         *  @param alignment 已经由 prepare_alignment 保证为不小于 16 的 2 的幂
         *  @return 按 alignment 对齐的用户指针；OS 拒绝或内部算术放不下时返回 nullptr
         *  @note 对齐小于 64KB 时，头在用户指针所在页的页首。对齐达到 64KB 时，用户指针自己就是页首，头改放到前一页。写到 user_ptr & 页掩码 会盖住用户数据
         */
        ID_NOINLINE void* allocate_large(size_t size, size_t alignment) noexcept
        {
            if (alignment < kCacheLineSize)
            {
                alignment = kCacheLineSize;
            }

            const size_t header_reserve = alignment >= SlabSpan::kSpanSize ? SlabSpan::kSpanSize : sizeof(LargeBlockHeader);
            if (alignment > SIZE_MAX - header_reserve || size > SIZE_MAX - header_reserve - alignment) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Error, "Large allocation size overflowed!");
                return nullptr;
            }

            const size_t total_needed = header_reserve + alignment + size;
            const size_t alloc_bytes  = (total_needed + (SlabSpan::kSpanSize - 1)) & ~(SlabSpan::kSpanSize - 1);
            void* raw_base = os_alloc_aligned_64k(alloc_bytes);
            if (!raw_base) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Fatal, "Failed to allocate large virtual memory block!");
                return nullptr;
            }

            const uintptr_t base_addr   = reinterpret_cast<uintptr_t>(raw_base);
            const uintptr_t min_user    = base_addr + header_reserve;
            const uintptr_t user_addr   = (min_user + alignment - 1) & ~(alignment - 1);
            uintptr_t header_addr       = user_addr & SlabSpan::kSpanMask;
            if ((user_addr & (SlabSpan::kSpanSize - 1)) == 0)
            {
                header_addr = user_addr - SlabSpan::kSpanSize;
            }
            const uintptr_t mapping_end = base_addr + alloc_bytes;

            if (header_addr < base_addr || header_addr + sizeof(LargeBlockHeader) > user_addr || user_addr + size > mapping_end) [[unlikely]]
            {
                os_free_aligned_64k(raw_base, alloc_bytes);
                ID_KERNEL_LOG(Log::Level::Error, "Large allocation could not place its header outside the user buffer!");
                return nullptr;
            }

            auto* header = reinterpret_cast<LargeBlockHeader*>(header_addr);
            header->magic                 = kLargeMagic;
            header->offset_to_user_ptr    = static_cast<uint32_t>(user_addr - header_addr);
            header->total_allocated_bytes = alloc_bytes;
            header->raw_base_address      = raw_base;

            g_large_allocation_bytes.fetch_add(alloc_bytes, std::memory_order_relaxed);
            return reinterpret_cast<void*>(user_addr);
        }

        /**
         *  @brief 从用户指针找回大对象头
         *  @param ptr ID_AlignedAlloc 返回的原指针，不能是块内部地址
         *  @return 匹配 magic 且 offset 指回 ptr 的头；对不上返回 nullptr
         *  @note 只在 ptr 本身 64KB 对齐时才读前一页。那一页和用户页属于同一次 OS 分配，不要对普通指针做这个探测
         */
        LargeBlockHeader* find_large_header(void* ptr) noexcept
        {
            const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            const uintptr_t page = addr & SlabSpan::kSpanMask;
            auto* same_page = reinterpret_cast<LargeBlockHeader*>(page);
            if (same_page->magic == kLargeMagic && page + same_page->offset_to_user_ptr == addr)
            {
                return same_page;
            }

            if ((addr & (SlabSpan::kSpanSize - 1)) != 0 || page < SlabSpan::kSpanSize)
            {
                return nullptr;
            }

            auto* prev_page = reinterpret_cast<LargeBlockHeader*>(page - SlabSpan::kSpanSize);
            if (prev_page->magic == kLargeMagic && reinterpret_cast<uintptr_t>(prev_page) + prev_page->offset_to_user_ptr == addr)
            {
                return prev_page;
            }
            return nullptr;
        }

        // 用户区可用字节数。头不一定位于 OS 块首，不能用 total_allocated_bytes - offset 代替
        size_t large_user_bytes(const LargeBlockHeader* header) noexcept
        {
            const uintptr_t user = reinterpret_cast<uintptr_t>(header) + header->offset_to_user_ptr;
            const uintptr_t end  = reinterpret_cast<uintptr_t>(header->raw_base_address) + header->total_allocated_bytes;
            if (end < user)
            {
                return 0;
            }
            return static_cast<size_t>(end - user);
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
            if (!prepare_alignment(alignment)) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Error, "ID_AlignedAlloc alignment must be a power of two!");
                return nullptr;
            }
            if (size > SIZE_MAX - (alignment - 1)) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Error, "ID_AlignedAlloc size overflowed while rounding up to alignment!");
                return nullptr;
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

            if (LargeBlockHeader* large_hdr = find_large_header(ptr))
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
            if (!prepare_alignment(alignment)) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Error, "ID_AlignedRealloc alignment must be a power of two!");
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
            else if (LargeBlockHeader* large_hdr = find_large_header(ptr))
            {
                old_capacity = large_user_bytes(large_hdr);
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
