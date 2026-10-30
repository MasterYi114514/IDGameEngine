#pragma once

#include "../Core/Core.hpp"

#include <cstddef>
#include <cstdint>

/**
 *  分段线性分配器（SegmentedArena）
 *  与 LinearArena 的差异：LinearArena 绑定调用方提供的一块固定缓冲区，容量用尽只能失败；
 *  SegmentedArena 按固定大小的段向来源申请内存并串成链，段内分配仍然是游标加法，
 *  只在段满时多一次来源分配，用略高的单次成本换取不受预分配大小限制的容量。
 *
 *  两类段：
 *  1. 常规段（Block）：固定 block_size 字节，游标加法分配，段满换下一段，reset() 后保留复用；
 *  2. 巨段（OversizeBlock）：单次请求超过阈值（常规段净容量的 1/4，下限 1 KiB、上限净容量）时，
 *     按"需求 + 最坏对齐填充"精确申请一段独立内存并挂到巨段链，整段一次用尽。
 *     这样几百 KB 级的长字符串不会因为"放不进一段"而分配失败，
 *     也不会为了一个请求独占大半个常规段、把剩余空间留成游标分配器无法回收的碎片。
 *
 *  不变量：
 *  1. 已分配的指针在 reset()/shutdown() 之前一直有效：段只申请、不搬迁，也不复用已发出的内存；
 *  2. reset() 只回退常规段游标并归还巨段（常规段内存继续可用），shutdown() 归还全部段；
 *  3. 失败一律返回 nullptr（未初始化、对齐非法、尺寸溢出、对齐填充超过常规段净容量、来源返回 nullptr），
 *     任一失败都不改变已经返回给调用方的内存。
 *
 *  典型用法（申请/归还函数由调用方提供，须满足 SegmentAllocFn/SegmentFreeFn 的契约）：
 *      ID::Common::SegmentedArena arena;
 *      arena.init(64 * 1024, &my_alloc, &my_free);
 *      void* p   = arena.allocate(128, 16);            // 常规段
 *      void* big = arena.allocate(300 * 1024, 16);     // 巨段
 *      arena.reset();      // 归还巨段，保留常规段，游标回退从头复用
 *      arena.shutdown();   // 归还全部段并恢复未初始化
 *
 *  @note 非线程安全，与 LinearArena 一样按单一所有者线程使用。reset()/shutdown() 会一次性作废所有
 *        已分配指针，且不调用任何析构函数，只应放置 trivially destructible 的数据。
 *  @note 巨段在 reset() 时归还：它按单次请求的精确尺寸申请，用尽后无法被后续请求复用，留着只会让大块
 *        内存长期驻留。free_fn 为空时，巨段与常规段一样只丢弃链、不归还，内存回收由调用方负责。
 */

namespace ID::Common
{
    using std::size_t;
    using std::uint8_t;

    // 段来源的申请/归还函数：size 为段的总字节数；申请方必须返回至少 alignof(std::max_align_t) 对齐的内存，
    // 失败返回 nullptr 且不得抛出，归还方必须接受申请方返回的原始指针
    using SegmentAllocFn = void*(*)(size_t size, size_t alignment) noexcept;
    using SegmentFreeFn  = void(*)(void* block) noexcept;

    // 缓存行对齐粒度：SegmentedArena 独占缓存行，避免与调用方的相邻数据产生伪共享。
    // Common/Core/Core.hpp 只提供调用宏、不提供缓存行常量，故常量定义在使用它的头里；
    // 名称带 Arena 前缀，避免与将来 Core.hpp 可能引入的通用同名常量冲突。
    // 取 64 字节：x86-64 与主流 ARM64 的缓存行宽度；更大的对齐只放大尾部浪费，更小则失去伪共享防护意义。
    inline constexpr size_t kArenaCacheLineSize = 64;

    class alignas(kArenaCacheLineSize) SegmentedArena
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
         *  @param block_size 每段的总字节数（含段头），必须大于 sizeof(Block)；同时决定巨段阈值
         *                    （净容量的 1/4，下限 1 KiB、上限净容量，取值依据见 allocate 的 @note）
         *  @param alloc_fn 段来源申请函数，不能为空；返回值必须至少 alignof(std::max_align_t) 对齐
         *  @param free_fn 段归还函数，可为空；为空时 shutdown()/reset() 只丢弃段链（含巨段），内存回收由调用方负责
         *  @return 配置成功返回 true；已初始化或参数非法返回 false（重复配置需先 shutdown()）
         *  @note 段大小建议命中分配器的尺寸档位（≤8KB），过大的段会走大对象页路径并放大内部碎片
         */
        bool init(size_t block_size, SegmentAllocFn alloc_fn, SegmentFreeFn free_fn = nullptr) noexcept;

        /**
         *  @brief 归还全部段（常规段与巨段）并恢复未初始化状态
         *  @note free_fn 为空时两条链都只丢弃、不归还来源内存，回收由调用方负责
         *  @note 调用后所有已分配指针失效，不调用任何析构函数；再次使用需重新 init()
         */
        void shutdown() noexcept;

        /**
         *  @brief 回退常规段游标并归还巨段，保留常规段内存供下一轮复用
         *  @note 已分配指针全部失效，且不调用任何析构函数：只应放置 trivially destructible 的数据
         *  @note 巨段按单次请求的精确尺寸申请、用尽后无法被后续请求复用，故在此归还来源；
         *        free_fn 为空时巨段与常规段一样只丢链、不归还，回收由调用方负责
         */
        void reset() noexcept;

        /**
         *  @brief 从当前段分配一块按 alignment 对齐的内存；超过巨段阈值的请求改为单独申请一段
         *  @param size 请求字节数，0 视为 1；超过巨段阈值时走巨段，不再受单段容量限制
         *  @param alignment 必须是 2 的幂；小于 alignof(std::max_align_t) 时抬到该值
         *  @return 对齐后的指针；未初始化、对齐非法、尺寸溢出、对齐填充超过常规段净容量
         *          或来源耗尽时返回 nullptr
         *  @note 常规段内分配是游标加法，只有当前段满时才调用一次 alloc_fn；失败不改变已分配数据。
         *  @note 巨段阈值 = 常规段净容量的 1/4，下限 1 KiB，上限为净容量本身。取 1/4 的理由：超过它的
         *        请求若仍走常规段，会把该段剩下的 3/4 空间留成游标分配器无法回收的碎片，而多一次来源
         *        申请的成本远低于长期驻留的段内碎片；下限 1 KiB 是因为碎片绝对量很小时不值得多付一次
         *        来源申请（来源头部开销与页对齐浪费），上限取净容量是为了不出现"比段还小却仍失败"的空洞区间。
         *  @note 巨段只挂链、不参与后续分配（申请时即用满），reset() 归还，shutdown() 与常规段一起归还。
         *  @note 请求尺寸超过常规段净容量不会失败，而是改走巨段；仍会失败的情形除未初始化与对齐非法外
         *        还有三种：尺寸溢出（巨段头 + 最坏对齐填充 + 请求超过 size_t 上限，见 allocate_oversize
         *        的逐项相减检查）、对齐填充超过常规段净容量（alignment 极大且请求仍落在常规段阈值内）、
         *        来源返回 nullptr。
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

        // 已申请的段数量（常规段 + 巨段）：巨段同样是向来源申请、受 free_fn 归还的段，且其净负载已计入
        // capacity()，两者口径必须一致；reset() 归还巨段后此值随之下降
        ID_FORCEINLINE size_t block_count() const noexcept { return m_block_count; }

        // 全部段的净负载容量之和（常规段 + 巨段，不含段头），未初始化时为 0；O(1)
        ID_FORCEINLINE size_t capacity() const noexcept { return m_capacity_bytes; }

        // 游标累计推进的字节数（含每次分配产生的对齐填充，不含段头；巨段只计入其填充与负载）
        ID_FORCEINLINE size_t used_bytes() const noexcept { return m_used_bytes; }

    private:
        // 巨段阈值分母：常规段净容量的 1/kOversizeDivisor。取 2 的幂，使除法在编译期折成一次右移
        static inline constexpr size_t kOversizeDivisor = 4;

        // 巨段阈值下限：碎片绝对量低于 1 KiB 时，多付一次来源申请（来源头部开销与页对齐浪费）
        // 比不上省下的段内碎片，此时继续走常规段更划算
        static inline constexpr size_t kOversizeMinBytes = 1024;

        // 常规段头，位于每段起始处；start 是本段当前的分配游标，reset() 时回退到 this + 1。
        // 巨段单独用 OversizeBlock：因此常规段头与改造前布局一致（x64 下为 16B，Win32 下为 8B），热路径布局不变
        struct Block
        {
            uint8_t*    start;
            Block*      next{nullptr};
        };

        // 巨段头，位于巨段起始处；bytes 为本巨段总字节数（含段头），reset()/shutdown() 归还时用于
        // 从 capacity() 中扣减。巨段按单次请求精确申请、一次用尽，故不需要分配游标
        struct OversizeBlock
        {
            OversizeBlock*  next{nullptr};
            size_t          bytes{0};
        };

        // 巨段填充公式（alignment - alignof(std::max_align_t)，见 allocate_oversize）依赖一个前提：
        // 来源返回的基址按 max_align 对齐时，段头之后（block + 1）也必须是 max_align 对齐，
        // 否则实际填充会超过按该公式算出的上界。段头尺寸为 max_align 的整数倍即保证这一点，零运行时成本
        // 断言消息有意写成 ASCII：MSVC 在非 UTF-8 代码页（如 936）下会把 UTF-8 中文的多字节序列
        // 误读为引号/转义，实测会报 C2001；中文说明留在上方注释里，断言文本不依赖源文件编码
        static_assert(sizeof(OversizeBlock) % alignof(std::max_align_t) == 0,
                      "OversizeBlock header must be a multiple of alignof(std::max_align_t): "
                      "the oversize padding formula relies on it");

        // 单段净负载容量（不含段头）
        ID_FORCEINLINE size_t payload_capacity() const noexcept { return m_block_size - sizeof(Block); }

        // 在指定常规段的游标处尝试分配；剩余空间不足时返回 nullptr
        ID_FORCEINLINE void* try_bump(Block* block, size_t size, size_t alignment) noexcept;

        // 向来源申请一个新常规段接到链尾并设为当前段；失败返回 false
        bool grow() noexcept;

        // 为单次超大请求申请一段恰好容纳它的巨段并挂到巨段链，返回对齐后的指针；失败返回 nullptr
        void* allocate_oversize(size_t size, size_t alignment) noexcept;

    private:
        SegmentAllocFn  m_alloc_fn{nullptr};            // 段来源申请函数
        SegmentFreeFn   m_free_fn{nullptr};             // 段来源归还函数
        Block*          m_head{nullptr};                // 常规段链首，reset() 后从此段开始复用
        Block*          m_current{nullptr};             // 当前正在填充的常规段
        OversizeBlock*  m_oversize_head{nullptr};       // 巨段链首，只由 reset()/shutdown() 归还
        size_t          m_block_size{0};                // 每段总字节数（含段头）
        size_t          m_oversize_threshold{0};        // 走巨段的请求尺寸阈值，init() 按段容量算出
        size_t          m_used_bytes{0};                // 游标累计推进字节数
        size_t          m_block_count{0};               // 已申请段数量（常规段 + 巨段）
        size_t          m_capacity_bytes{0};            // 段净负载容量之和（常规段 + 巨段）
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

        // 巨段阈值：净容量的 1/kOversizeDivisor，下限 kOversizeMinBytes，上限为净容量本身。
        // 上限是必需的：阈值一旦高于净容量，介于净容量与阈值之间的请求既超过常规段能力、又没到巨段条件，
        // 会形成"比段还小却必然失败"的空洞区间。取上限后本类只有两个分支：塞得进常规段，或走巨段
        const size_t payload   = payload_capacity();
        const size_t quarter   = payload / kOversizeDivisor;
        size_t       threshold = quarter > kOversizeMinBytes ? quarter : kOversizeMinBytes;
        if (threshold > payload)
        {
            threshold = payload;
        }
        m_oversize_threshold = threshold;
        return true;
    }

    inline void SegmentedArena::shutdown() noexcept
    {
        // 两条链各自申请、各自归还；free_fn 为空时都只丢弃链，内存回收交给调用方
        if (m_free_fn != nullptr)
        {
            for (Block* block = m_head; block != nullptr; )
            {
                Block* next = block->next;
                m_free_fn(block);
                block = next;
            }
            for (OversizeBlock* block = m_oversize_head; block != nullptr; )
            {
                OversizeBlock* next = block->next;
                m_free_fn(block);
                block = next;
            }
        }

        m_alloc_fn           = nullptr;
        m_free_fn            = nullptr;
        m_head               = nullptr;
        m_current            = nullptr;
        m_oversize_head      = nullptr;
        m_block_size         = 0;
        m_oversize_threshold = 0;
        m_used_bytes         = 0;
        m_block_count        = 0;
        m_capacity_bytes     = 0;
    }

    inline void SegmentedArena::reset() noexcept
    {
        // 巨段按单次请求精确申请、申请时即用满，下一轮无法被复用，留着只会让大块内存长期驻留；
        // 因此 reset() 归还巨段，常规段保留供下一轮复用
        for (OversizeBlock* block = m_oversize_head; block != nullptr; )
        {
            OversizeBlock* next = block->next;
            m_capacity_bytes -= block->bytes - sizeof(OversizeBlock);
            --m_block_count;
            if (m_free_fn != nullptr)
            {
                m_free_fn(block);
            }
            block = next;
        }
        m_oversize_head = nullptr;

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
        m_capacity_bytes += payload_capacity();
        return true;
    }

    inline void* SegmentedArena::allocate_oversize(size_t size, size_t alignment) noexcept
    {
        // 巨段按"恰好容纳本次请求"申请：段头 + 最坏对齐填充 + 请求；整段一次用尽，不参与后续分配。
        // 段基址由来源保证至少 max_align 对齐，故实际填充不会超过 alignment - max_align
        const size_t padding = alignment > alignof(std::max_align_t) ? alignment - alignof(std::max_align_t) : 0;

        // 尺寸溢出检查：先逐项相减再相加，避免 sizeof + padding + size 一次性相加时回绕成小值
        const size_t max_size = static_cast<size_t>(-1);
        if (padding > max_size - sizeof(OversizeBlock)) [[unlikely]]
        {
            return nullptr;
        }
        const size_t headroom = sizeof(OversizeBlock) + padding;
        if (size > max_size - headroom) [[unlikely]]
        {
            return nullptr;
        }
        const size_t total = headroom + size;

        void* raw = m_alloc_fn(total, alignof(std::max_align_t));
        if (raw == nullptr) [[unlikely]]
        {
            return nullptr;
        }

        auto* block             = static_cast<OversizeBlock*>(raw);
        const uintptr_t cursor  = reinterpret_cast<uintptr_t>(block + 1);
        const uintptr_t aligned = (cursor + alignment - 1) & ~(alignment - 1);
        if (aligned < cursor) [[unlikely]]
        {
            // 地址空间回绕：真实进程里取不到这样的地址，仍显式归还以免泄漏，且不改变任何已分配数据
            if (m_free_fn != nullptr)
            {
                m_free_fn(block);
            }
            return nullptr;
        }

        block->next     = m_oversize_head;
        block->bytes    = total;
        m_oversize_head = block;

        ++m_block_count;
        m_capacity_bytes += total - sizeof(OversizeBlock);
        m_used_bytes     += static_cast<size_t>(aligned + size - cursor);
        return reinterpret_cast<void*>(aligned);
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

        // 超过阈值（含"比常规段净容量还大"）的请求改走巨段：放不进常规段，或者放得进但会独占大半个段
        if (size > m_oversize_threshold) [[unlikely]]
        {
            return allocate_oversize(size, alignment);
        }

        // 阈值不超过净容量（init() 已夹紧），故此处 size <= payload 必然成立；剩下的失败情形是对齐填充
        // 超过净容量（alignment 极大），直接失败而不是浪费一次来源分配
        const size_t payload = payload_capacity();
        const size_t padding = alignment > alignof(std::max_align_t) ? alignment - alignof(std::max_align_t) : 0;
        if (padding > payload - size) [[unlikely]]
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