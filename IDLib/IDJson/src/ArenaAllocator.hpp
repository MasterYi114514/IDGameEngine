#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace ID
{
    // ═══════════════════════════════════════════
    //  ArenaAllocator<T> — 给 std::vector / std::map 用的 STL 适配器
    //
    //  注意：
    //    — deallocate 是 no-op（Arena 不单独释放）
    //    — 扩容时旧块被"遗忘"在 Arena 中，不会归还
    //    — 对于一次性构建的 JSON 解析，这不是问题
    // ═══════════════════════════════════════════
    template<typename T>
    class ArenaAllocator
    {
    public:
        using value_type = T;

        Arena* arena;

        ArenaAllocator(Arena& a) noexcept : arena(&a) {}

        template<typename U>
        ArenaAllocator(const ArenaAllocator<U>& other) noexcept : arena(other.arena) {}

        T* allocate(size_t n)
        {
            return static_cast<T*>(arena->allocate(n * sizeof(T), alignof(T)));
        }

        void deallocate(T*, size_t) noexcept {}

        template<typename U>
        bool operator==(const ArenaAllocator<U>& other) const noexcept
        {
            return arena == other.arena;
        }

        template<typename U>
        bool operator!=(const ArenaAllocator<U>& other) const noexcept
        {
            return !(*this == other);
        }
    };
}