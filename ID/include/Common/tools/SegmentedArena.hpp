#pragma once

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  分段线性分配器（SegmentedArena）
 *  与 LinearArena 的差异：LinearArena 绑定调用方提供的一块固定缓冲区，容量用尽只能失败；
 *  SegmentedArena 按固定大小的段（Block）向来源申请内存并串成链，段内分配仍然是游标加法，
 *  只在段满时多一次来源分配，用略高的单次成本换取不受预分配大小限制的容量。
 *
 *  典型用法（来源可直接注入内核统一分配器）：
 *      ID::SegmentedArena arena;
 *      arena.init(64 * 1024, &ID::Memory::ID_AlignedAlloc, &ID::Memory::ID_AlignedFree);
 *      void* p = arena.allocate(128, 16);
 *      arena.reset();      // 保留全部段，游标回退，从头复用
 *      arena.shutdown();   // 归还全部段并恢复未初始化
 *
 *  @note 非线程安全，与 LinearArena 一样按单一所有者线程使用。reset() 会一次性作废所有已分配指针，
 *        且不调用任何析构函数，只应放置 trivially destructible 的数据。
 */

namespace ID
{
    // 段来源的申请/归还函数：size 为段的总字节数；申请方必须返回至少 alignof(std::max_align_t) 对齐的内存，
    // 失败返回 nullptr 且不得抛出，归还方必须接受申请方返回的原始指针
    using SegmentAllocFn = void*(*)(size_t size, size_t alignment) noexcept;
    using SegmentFreeFn  = void(*)(void* block) noexcept;

    class alignas(kCacheLineSize) SegmentedArena
    {
    public:
        constexpr SegmentedArena() noexcept = default;

        SegmentedArena(const SegmentedArena&)            = delete;
        SegmentedArena& operator=(const SegmentedArena&) = delete;

        ~SegmentedArena() noexcept
        {
            shutdown();
        }

        /**
         *  @brief 配置段大小与内存来源，之后才能调用 allocate
         *  @param block_size 每段的总字节数（含段头），必须大于 sizeof(Block)
         *  @param alloc_fn 段来源申请函数，不能为空；返回值必须至少 alignof(std::max_align_t) 对齐
         *  @param free_fn 段归还函数，可为空；为空时 shutdown() 只丢弃段链，内存回收由调用方负责
         *  @return 配置成功返回 true；已初始化或参数非法返回 false（重复配置需先 shutdown()）
         *  @note 段大小建议命中内核 Slab 档位（≤8KB），过大的段会走大对象页路径并放大内部碎片
         */
        bool init(size_t block_size, SegmentAllocFn alloc_fn, SegmentFreeFn free_fn = nullptr) noexcept;

        // 归还全部段并恢复未初始化状态；未提供 free_fn 时只丢弃段链
        void shutdown() noexcept;

        // 所有段游标回退到段头之后并保留内存，供下一轮复用；所有已分配指针立即失效
        void reset() noexcept;

        /**
         *  @brief 从当前段分配一块按 alignment 对齐的内存
         *  @param size 请求字节数，0 视为 1；不得超过 block_size - sizeof(Block)
         *  @param alignment 必须是 2 的幂；小于 alignof(std::max_align_t) 时抬到该值
         *  @return 对齐后的指针；未初始化、参数非法、单段放不下或来源耗尽时返回 nullptr
         *  @note 段内分配是游标加法，只有当前段满时才调用一次 alloc_fn；失败不改变已分配数据
         */
        void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;

        // 类型安全的数组分配；元素总字节数溢出时返回 nullptr
        template <typename T>
        T* allocate_array(size_t count = 1) noexcept
        {
            if (count != 0 && count > static_cast<size_t>(-1) / sizeof(T)) [[unlikely]]
            {
                return nullptr;
            }
            return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
        }

        ID_FORCEINLINE bool is_initialized() const noexcept { return m_alloc_fn != nullptr; }

        ID_FORCEINLINE size_t block_size() const noexcept { return m_block_size; }

        ID_FORCEINLINE size_t block_count() const noexcept { return m_block_count; }

        // 全部段的净负载容量（不含段头），未初始化时为 0
        ID_FORCEINLINE size_t capacity() const noexcept
        {
            return m_block_count == 0 ? 0 : m_block_count * (m_block_size - sizeof(Block));
        }

        // 游标累计推进的字节数（含每次分配产生的对齐填充）
        ID_FORCEINLINE size_t used_bytes() const noexcept { return m_used_bytes; }

    private:
        // 段头，位于每段起始处；start 是本段当前的分配游标，reset() 时回退到 this + 1
        struct Block
        {
            uint8_t*    start;
            Block*      next{nullptr};
        };

        // 单段净负载容量（不含段头）
        ID_FORCEINLINE size_t payload_capacity() const noexcept { return m_block_size - sizeof(Block); }

        // 在指定段的游标处尝试分配；剩余空间不足时返回 nullptr
        ID_FORCEINLINE void* try_bump(Block* block, size_t size, size_t alignment) noexcept;

        // 向来源申请一个新段接到链尾并设为当前段；失败返回 false
        bool grow() noexcept;

    private:
        SegmentAllocFn  m_alloc_fn{nullptr};        // 段来源申请函数
        SegmentFreeFn   m_free_fn{nullptr};         // 段来源归还函数
        Block*          m_head{nullptr};            // 链首，reset() 后从此段开始复用
        Block*          m_current{nullptr};         // 当前正在填充的段
        size_t          m_block_size{0};            // 每段总字节数（含段头）
        size_t          m_used_bytes{0};            // 游标累计推进字节数
        size_t          m_block_count{0};           // 已申请的段数量
    };

    inline bool SegmentedArena::init(size_t block_size, SegmentAllocFn alloc_fn, SegmentFreeFn free_fn) noexcept
    {
        if (m_alloc_fn != nullptr || m_head != nullptr) [[unlikely]]
        {
            return false;   // 已初始化，必须先 shutdown()
        }
        if (alloc_fn == nullptr || block_size <= sizeof(Block)) [[unlikely]]
        {
            return false;
        }

        m_alloc_fn   = alloc_fn;
        m_free_fn    = free_fn;
        m_block_size = block_size;
        return true;
    }

    inline void SegmentedArena::shutdown() noexcept
    {
        if (m_free_fn != nullptr)
        {
            for (Block* block = m_head; block != nullptr; )
            {
                Block* next = block->next;
                m_free_fn(block);
                block = next;
            }
        }

        m_alloc_fn    = nullptr;
        m_free_fn     = nullptr;
        m_head        = nullptr;
        m_current     = nullptr;
        m_block_size  = 0;
        m_used_bytes  = 0;
        m_block_count = 0;
    }

    inline void SegmentedArena::reset() noexcept
    {
        for (Block* block = m_head; block != nullptr; block = block->next)
        {
            block->start = reinterpret_cast<uint8_t*>(block + 1);
        }
        m_current    = m_head;
        m_used_bytes = 0;
    }

    ID_FORCEINLINE void* SegmentedArena::try_bump(Block* block, size_t size, size_t alignment) noexcept
    {
        const uintptr_t cursor  = reinterpret_cast<uintptr_t>(block->start);
        const uintptr_t aligned = (cursor + alignment - 1) & ~(alignment - 1);
        const uintptr_t end     = reinterpret_cast<uintptr_t>(block) + m_block_size;

        if (aligned < cursor || aligned > end || size > end - aligned) [[unlikely]]
        {
            return nullptr;
        }

        block->start  = reinterpret_cast<uint8_t*>(aligned + size);
        m_used_bytes += static_cast<size_t>(aligned + size - cursor);
        return reinterpret_cast<void*>(aligned);
    }

    inline bool SegmentedArena::grow() noexcept
    {
        void* raw = m_alloc_fn(m_block_size, alignof(std::max_align_t));
        if (raw == nullptr) [[unlikely]]
        {
            return false;
        }

        auto* block  = static_cast<Block*>(raw);
        block->start = reinterpret_cast<uint8_t*>(block + 1);
        block->next  = nullptr;

        if (m_current != nullptr)
        {
            m_current->next = block;
        }
        else
        {
            m_head = block;
        }
        m_current = block;
        ++m_block_count;
        return true;
    }

    inline void* SegmentedArena::allocate(size_t size, size_t alignment) noexcept
    {
        if (m_alloc_fn == nullptr) [[unlikely]]
        {
            return nullptr;
        }

        if (size == 0)
        {
            size = 1;
        }
        if (alignment < alignof(std::max_align_t))
        {
            alignment = alignof(std::max_align_t);
        }
        if ((alignment & (alignment - 1)) != 0) [[unlikely]]
        {
            return nullptr;
        }

        // 单段必须放得下最坏情况（请求 + 最大对齐填充），否则新段同样放不下，直接失败而不是浪费一次来源分配；
        // 段基址至少 max_align 对齐，故实际填充不会超过 alignment - max_align
        const size_t payload = payload_capacity();
        const size_t padding = alignment > alignof(std::max_align_t) ? alignment - alignof(std::max_align_t) : 0;
        if (size > payload || padding > payload - size) [[unlikely]]
        {
            return nullptr;
        }

        if (m_current == nullptr)
        {
            return grow() ? try_bump(m_current, size, alignment) : nullptr;
        }

        if (void* ptr = try_bump(m_current, size, alignment); ptr != nullptr) [[likely]]
        {
            return ptr;
        }

        // 当前段已满：优先复用链上的下一段（reset() 之后存在），否则向来源申请新段并接到链尾
        if (m_current->next != nullptr)
        {
            m_current = m_current->next;
        }
        else if (!grow()) [[unlikely]]
        {
            return nullptr;
        }
        return try_bump(m_current, size, alignment);
    }
} // namespace ID
