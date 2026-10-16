#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  64KB 对齐的无锁双链表 Slab 工具（SlabSpan）
 *  采用双缓存行隔离设计：
 *  - 第一缓存行（64B）：所属线程私有读写（local_free_list），分配与同线程释放均为零原子指令；
 *  - 第二缓存行（64B）：跨线程无锁 CAS 归还链表（remote_free_list），彻底消除伪共享。
 */

namespace ID
{
    struct SlabFreeNode
    {
        SlabFreeNode* next;
    };

    struct alignas(kCacheLineSize) SlabSpan
    {
        static inline constexpr size_t      kSpanSize  = 64 * 1024;                                 // 64KB 对齐的 Slab 大小
        static inline constexpr uintptr_t   kSpanMask  = ~(static_cast<uintptr_t>(kSpanSize - 1));  // 64KB 对齐掩码
        static inline constexpr uint32_t    kSlabMagic = 0x49445342u;                               // 'IDSB' 的 16 进制表示，用于 SlabSpan 的魔数校验

        // --- Cache Line 0: 所属线程私有数据 ---
        uint32_t        magic               = 0;        // 魔数标识，区分 SlabSpan 与大对象块
        uint32_t        size_class_idx      = 0;        // 所属 Size Class 档位下标 (0 ~ 14)
        uint32_t        block_size          = 0;        // 单个分块的字节大小
        uint32_t        capacity            = 0;        // 当前 Slab 可容纳的总块数
        uintptr_t       owner_thread_tag    = 0;        // 创建该 Slab 的线程唯一标识（TLS 地址）
        SlabFreeNode*   local_free_list     = nullptr;  // 本线程私有空闲链表（零原子开销）
        SlabSpan*       next_slab           = nullptr;  // 同一线程同档位的下一个 Slab 指针
        uint64_t        reserved_pad        = 0;        // 填充至恰好 64 字节（1 个缓存行）

        // --- Cache Line 1: 跨线程无锁归还区（独立缓存行隔离伪共享） ---
        alignas(kCacheLineSize) std::atomic<SlabFreeNode*> remote_free_list{ nullptr };

        /**
         *  @brief 在 64KB 对齐的原始内存块上格式化 SlabSpan 元数据，并初始化空闲链表
         *  @param raw_64k_memory 64KB 对齐的原始内存块指针，必须独占一页，本函数不向 OS 申请内存
         *  @param class_idx 所属 Size Class 档位下标 (0 ~ 14)
         *  @param blk_size 单个分块的字节大小，必须不小于一个指针
         *  @param owner_tag 创建该 Slab 的线程唯一标识（TLS 地址）
         *  @return 格式化后的 SlabSpan 指针，与 raw_64k_memory 是同一地址
         *  @note 块起始按「能整除 blk_size 的最大 2 的幂」对齐。48/96/192/384/768 不是 2 的幂，不能用 ~(blk_size - 1)
         */
        static SlabSpan* format(void* raw_64k_memory, uint32_t class_idx, uint32_t blk_size, uintptr_t owner_tag) noexcept
        {
            auto* slab              = static_cast<SlabSpan*>(raw_64k_memory);
            slab->magic             = kSlabMagic;
            slab->size_class_idx    = class_idx;
            slab->block_size        = blk_size;
            slab->owner_thread_tag  = owner_tag;
            slab->next_slab         = nullptr;
            slab->reserved_pad      = 0;
            slab->remote_free_list.store(nullptr, std::memory_order_relaxed);

            uint32_t block_align = 1;
            while (block_align <= (blk_size >> 1) && (blk_size & ((block_align << 1) - 1)) == 0)
            {
                block_align <<= 1;
            }

            uintptr_t span_start = reinterpret_cast<uintptr_t>(raw_64k_memory);
            uintptr_t align_mask = static_cast<uintptr_t>(block_align) - 1;
            uintptr_t data_start = (span_start + sizeof(SlabSpan) + align_mask) & ~align_mask;
            uintptr_t span_end   = span_start + kSpanSize;

            if (blk_size < sizeof(SlabFreeNode))
            {
                slab->local_free_list = nullptr;
                slab->capacity        = 0;
                return slab;
            }

            // 正序串联单向空闲链表（保证低地址优先分配，提高 CPU 缓存局部性）
            SlabFreeNode* head  = nullptr;
            SlabFreeNode* tail  = nullptr;
            uint32_t      count = 0;

            for (uintptr_t addr = data_start; addr + blk_size <= span_end; addr += blk_size)
            {
                auto* node = reinterpret_cast<SlabFreeNode*>(addr);
                node->next = nullptr;
                if (!head)
                {
                    head = node;
                    tail = node;
                }
                else
                {
                    tail->next = node;
                    tail       = node;
                }
                ++count;
            }

            slab->local_free_list = head;
            slab->capacity        = count;
            return slab;
        }

        /**
         *  @brief 从本线程空闲链表弹出一块内存
         *  @return 块指针；链表空时返回 nullptr，调用方再走 harvest_remote 或新页
         *  @note 只碰 local_free_list，没有原子指令。只有所属线程可以调用
         */
        ID_FORCEINLINE void* allocate_local() noexcept
        {
            if (SlabFreeNode* node = local_free_list; node != nullptr) [[likely]]
            {
                local_free_list = node->next;
                return node;
            }
            return nullptr;
        }

        /**
         *  @brief 一次性摘下跨线程归还链表，交给本线程继续分配
         *  @return 摘下的第一块；对端链表空时返回 nullptr
         *  @note local_free_list 为空时只摘头节点，其余整链留下。非空时把对端整链接到本线程链表前面，禁止直接覆盖，否则已有空闲块会丢
         */
        ID_FORCEINLINE void* harvest_remote() noexcept
        {
            SlabFreeNode* remote = remote_free_list.exchange(nullptr, std::memory_order_acquire);
            if (remote == nullptr)
            {
                return nullptr;
            }

            if (local_free_list == nullptr) [[likely]]
            {
                local_free_list = remote->next;
                return remote;
            }

            SlabFreeNode* tail = remote;
            while (tail->next != nullptr)
            {
                tail = tail->next;
            }
            tail->next      = local_free_list;
            local_free_list = remote->next;
            return remote;
        }

        /**
         *  @brief 释放一块由本 Span 切出的内存
         *  @param ptr 必须是本 Span 的块起始地址，不能是块内部指针
         *  @param current_thread_tag 当前线程标记，用来区分同线程头插和跨线程 CAS
         *  @note 同线程只写 local_free_list，别的线程不能碰。跨线程只挂 remote_free_list
         */
        ID_FORCEINLINE void deallocate(void* ptr, uintptr_t current_thread_tag) noexcept
        {
            auto* node = static_cast<SlabFreeNode*>(ptr);
            if (owner_thread_tag == current_thread_tag) [[likely]]
            {
                node->next      = local_free_list;
                local_free_list = node;
            }
            else
            {
                SlabFreeNode* head = remote_free_list.load(std::memory_order_relaxed);
                do
                {
                    node->next = head;
                } while (!remote_free_list.compare_exchange_weak(head, node, std::memory_order_release, std::memory_order_relaxed));
            }
        }

        /**
         *  @brief 用 64KB 掩码从任意指针反查所在页的头部
         *  @param ptr 必须落在本分配器交出的 64KB 页内
         *  @return 该页页首。函数不检查这块页是不是 Slab，调用方要再看 magic
         *  @note 页首不一定是大对象头。用户指针本身按 64KB 对齐时，大对象头在前一页
         */
        ID_FORCEINLINE static SlabSpan* from_ptr(void* ptr) noexcept
        {
            return reinterpret_cast<SlabSpan*>(reinterpret_cast<uintptr_t>(ptr) & kSpanMask);
        }
    };

    static_assert(sizeof(SlabSpan) == 2 * kCacheLineSize, "SlabSpan must occupy exactly 2 cache lines (128 bytes)");
} // namespace ID
