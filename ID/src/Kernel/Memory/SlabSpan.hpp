#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

namespace ID::Memory
{
    // 侵入式链表，它的数据存放在空闲块里
    struct SlabFreeNode
    {
        SlabFreeNode* next;
    };

    /**
     *  @brief 按照 64B 对其的页分配器，放在页首，用于管理此页
     *  @note 1. Memory 会一次性向 OS 申请 64 KB 的连续的虚拟内存页，每个页会拆成若干相同大小的分块（每个块的大小就是 Size CLass），一个分块就是一个 Block，整个均分成块的页是一个 Slab。
     *  @note 2. SlabSpan 本身占用 128B （2 个 CacheLine），放在页的起始位置，用于存储此页的信息。
     *  @note 3. Slab 的数据可能会被别的线程使用，SlabSpan 内部记录了它的 owner thread
     */
    struct alignas(kCacheLineSize) SlabSpan
    {
        // 64KB 对齐的 Slab 大小
        static inline constexpr size_t      kSpanSize  = 64 * 1024;                                 

        // 64KB 对齐掩码
        static inline constexpr uintptr_t   kSpanMask  = ~(static_cast<uintptr_t>(kSpanSize - 1));  

        // 'IDSB' 的 16 进制表示，用于 SlabSpan 的魔数校验
        static inline constexpr uint32_t    kSlabMagic = 0x49445342u;                               

        // ========== CacheLine 0: 所属线程私有数据 ==========

        // 魔数标识，用于表示此页是 Slab 页，大对象页用 `IDLG` 的魔数
        uint32_t        magic               = kSlabMagic;

        // 每个 Block 的大小
        uint32_t        block_size          = 0;

        // 当前 Slab 可容纳的总块数
        uint32_t        capacity            = 0;

        // 填充至恰好 16 字节（1/4 个缓存行）
        uint32_t        __pad0              = 0;    

        // 当前 Slab 的 owner thread
        uintptr_t       owner_thread_tag    = 0;

        // owner thread 通过 local_free_list 维护空闲链表，确保无原子操作的额外开销
        SlabFreeNode*   local_free_list     = nullptr;

        // 同一线程同档位的 Slab 通过 next_slab 串成链表，便于快速遍历
        SlabSpan*       next_slab           = nullptr;

        // 填充至恰好 64 字节（1 个缓存行）
        uint64_t        __pad1[3]     = {0};

        // ========== CacheLine 1: 跨线程无锁归还区 ==========

        /**
         *  @brief 跨线程归还的空闲链表，使用原子操作维护，确保线程安全
         *  @note 1. 允许其它线程归还在本线程分配的块，归还的块会被挂到 remote_free_list 上，等待本线程收割
         *  @note 2. 由于 remote_free_list 可能会被多个线程访问，因此将其内存放置在一个单独的 CacheLine 上，使得多个线程同时争抢时不会影响 SlabSpan 的其它成员被访问，解决伪共享问题。
         */
        alignas(kCacheLineSize) std::atomic<SlabFreeNode*> remote_free_list{ nullptr };

        /**
         *  @brief 为一页 64KB 的内存进行格式化，初始化 SlabSpan 结构体，并构建空闲链表
         *  @param raw_64k_memory 64KB 对齐的原始内存块指针，必须独占一页，本函数不向 OS 申请内存
         *  @param blk_size 单个分块的字节大小，必须不小于一个指针
         *  @param owner_tag 创建该 Slab 的线程唯一标识（TLS 地址）
         *  @return 格式化后的 SlabSpan 指针，与 raw_64k_memory 是同一地址
         */
        static SlabSpan* format(void* raw_64k_memory, uint32_t blk_size, uintptr_t owner_tag) noexcept;

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
} // namespace ID::Memory
