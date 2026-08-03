#include "Arena.hpp"

#include <algorithm>
#include <cstdlib>

namespace ID
{
    Arena::Arena(size_t capacity)
    {
        capacity = std::max(capacity, size_t(64));

        m_head = static_cast<Block*>(std::malloc(sizeof(Block)));
        if(!m_head)
            std::abort();

        m_head->data = static_cast<char*>(std::malloc(capacity));
        m_head->size = capacity;
        m_head->next = nullptr;
        m_offset     = 0;

        if(!m_head->data)
        {
            std::free(m_head);
            std::abort();
        }
    }

    Arena::Arena(Arena&& other) noexcept
        : m_head(other.m_head)
        , m_offset(other.m_offset)
    {
        other.m_head   = nullptr;
        other.m_offset = 0;
    }

    Arena& Arena::operator=(Arena&& other) noexcept
    {
        if(this != &other)
        {
            destroy();

            m_head   = other.m_head;
            m_offset = other.m_offset;

            other.m_head   = nullptr;
            other.m_offset = 0;
        }
        return *this;
    }

    void* Arena::allocate(size_t n, size_t alignment)
    {
        size_t padded = (m_offset + alignment - 1) & ~(alignment - 1);

        if(padded + n > m_head->size)
        {
            // 空间不足 → 分配新 Block（2× 增长）
            size_t new_size = m_head->size * 2;
            while(padded + n > new_size)
            {
                // 防止 size_t 溢出
                if(new_size > SIZE_MAX / 2)
                {
                    new_size = padded + n;
                    break;
                }
                new_size *= 2;
            }

            Block* new_block = static_cast<Block*>(std::malloc(sizeof(Block)));
            if(!new_block)
                std::abort();

            new_block->data = static_cast<char*>(std::malloc(new_size));
            if(!new_block->data)
            {
                std::free(new_block);
                std::abort();
            }
            new_block->size = new_size;
            new_block->next = m_head;

            m_head   = new_block;
            m_offset = 0;
            padded   = 0;
        }

        void* ptr = m_head->data + padded;
        m_offset  = padded + n;
        return ptr;
    }

    void Arena::reset()
    {
        if(!m_head)
            return;

        // 释放除首 Block 外的所有 Block
        Block* block = m_head->next;
        while(block)
        {
            Block* next = block->next;
            std::free(block->data);
            std::free(block);
            block = next;
        }
        m_head->next = nullptr;
        m_offset     = 0;
    }

    void Arena::destroy()
    {
        Block* block = m_head;
        while(block)
        {
            Block* next = block->next;
            std::free(block->data);
            std::free(block);
            block = next;
        }
        m_head   = nullptr;
        m_offset = 0;
    }

} // namespace ID
