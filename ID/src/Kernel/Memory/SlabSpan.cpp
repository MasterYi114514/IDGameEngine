#include "SlabSpan.hpp"

namespace ID::Memory
{
    namespace
    {
        ID_FORCEINLINE size_t calculate_free_list_begin(uint32_t blk_size) noexcept
        {
            if (blk_size <= sizeof(SlabSpan)) return sizeof(SlabSpan);
            return blk_size;
        }
    } // namespace

    SlabSpan* SlabSpan::format(void* raw_64k_memory, uint32_t blk_size, uintptr_t owner_tag) noexcept
    {
        // 设置页首的 SlabSpan
        SlabSpan* span = static_cast<SlabSpan*>(raw_64k_memory);
        span->magic             = kSlabMagic;
        span->block_size        = blk_size;
        span->capacity          = 0;
        span->owner_thread_tag  = owner_tag;
        span->local_free_list   = nullptr;
        span->next_slab         = nullptr;
        span->remote_free_list.store(nullptr, std::memory_order_relaxed);
    
        // 块太小，放不下空闲链表节点，无法切分
        if (blk_size < sizeof(SlabFreeNode))
        {
            return span;
        }

        // 从页头之后（或按块对齐）开始，按块大小切分并串成升序空闲链表
        const uintptr_t page_begin = reinterpret_cast<uintptr_t>(raw_64k_memory);
        const uintptr_t data_begin = page_begin + calculate_free_list_begin(blk_size);
        const uintptr_t page_end   = page_begin + kSpanSize;

        SlabFreeNode* head  = nullptr;
        SlabFreeNode* tail  = nullptr;
        uint32_t      count = 0;

        for (uintptr_t addr = data_begin; addr + blk_size <= page_end; addr += blk_size)
        {
            auto* node = reinterpret_cast<SlabFreeNode*>(addr);
            node->next = nullptr;
            if (head == nullptr)
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

        span->local_free_list = head;
        span->capacity        = count;
        return span;
    }
} // namespace ID::Memory