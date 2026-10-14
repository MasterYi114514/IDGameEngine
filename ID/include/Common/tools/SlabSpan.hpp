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

        // 在一块 64KB 对齐的原始内存上初始化 SlabSpan 并切分侵入式空闲链表
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

            uintptr_t span_start = reinterpret_cast<uintptr_t>(raw_64k_memory);
            uintptr_t data_start = (span_start + sizeof(SlabSpan) + (blk_size - 1)) & ~static_cast<uintptr_t>(blk_size - 1);
            uintptr_t span_end   = span_start + kSpanSize;

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

        // 本线程快路径分配：直接从 local_free_list 弹出头节点，零原子指令
        ID_FORCEINLINE void* allocate_local() noexcept
        {
            if (SlabFreeNode* node = local_free_list; node != nullptr) [[likely]]
            {
                local_free_list = node->next;
                return node;
            }
            return nullptr;
        }

        // 慢路径：一次性原子收割跨线程归还的 remote_free_list 到本线程 local_free_list
        ID_FORCEINLINE void* harvest_remote() noexcept
        {
            SlabFreeNode* remote = remote_free_list.exchange(nullptr, std::memory_order_acquire);
            if (remote != nullptr)
            {
                local_free_list = remote->next;
                return remote;
            }
            return nullptr;
        }

        // 释放内存块：同线程走零原子头插，跨线程走无锁 CAS 挂入 remote_free_list
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

        // O(1) 地址掩码反查：从任意对象指针用 1 条按位与指令定位所属 64KB SlabSpan
        ID_FORCEINLINE static SlabSpan* from_ptr(void* ptr) noexcept
        {
            return reinterpret_cast<SlabSpan*>(reinterpret_cast<uintptr_t>(ptr) & kSpanMask);
        }
    };

    static_assert(sizeof(SlabSpan) == 2 * kCacheLineSize, "SlabSpan must occupy exactly 2 cache lines (128 bytes)");
} // namespace ID
