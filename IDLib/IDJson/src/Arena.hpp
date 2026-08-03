#pragma once

#include <cstdint>
#include <cstddef>
#include <new>
#include <utility>

#include "ArenaManager.hpp"

namespace ID
{
    /*
    *   Arena — 基于链表式多 Block 的 bump-pointer 分配器。
    *
    *   当当前 Block 空间不足时分配新 Block（旧 Block 保留不动），
    *   保证已分配对象的地址永不失效。destroy() 时统一释放所有 Block。
    *
    *   重要成员：
    *     - m_head   : Block 链表头，也是当前正在分配的 Block
    *     - m_offset : 当前 Block 内已使用的偏移量
    */
    class Arena
    {
        struct Block
        {
            Block*  next;
            char*   data;
            size_t  size;
        };

        Block*  m_head   = nullptr;
        size_t  m_offset = 0;

    public:
        explicit Arena(size_t capacity);
        ~Arena() { destroy(); }

        // 禁止拷贝
        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;

        // 允许移动
        Arena(Arena&& other) noexcept;
        Arena& operator=(Arena&& other) noexcept;

        /*
        *   从当前 Block 分配 n 字节（对齐到 alignment）。
        *   空间不足时分配新 Block（2× 增长），旧 Block 保留以保证指针稳定。
        */
        void*   allocate(size_t n, size_t alignment = alignof(std::max_align_t));
        void    reset();      // 保留首 Block，释放其余 Block，偏移归零
        void    destroy();    // 释放所有 Block

        template<typename T, typename... Args>
        T* create(Args&&... args)
        {
            void* ptr = allocate(sizeof(T), alignof(T));
            return new (ptr) T(std::forward<Args>(args)...);
        }
    };

    Arena& get_arena(ArenaID arena_id);
} // namespace ID