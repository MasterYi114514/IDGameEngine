#pragma once

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  通用线性内存分配器工具（LinearArena）
 *  在给定的连续内存缓冲区上通过单次指针加法完成极速分配，支持 O(1) 整体重置。
 */

namespace ID
{
    using ArenaFallbackFn = void(*)(size_t size, size_t alignment) noexcept;

    // 线性内存分配器类，提供在预分配缓冲区上进行快速内存分配的功能
    class alignas(kCacheLineSize) LinearArena
    {
    public:
        constexpr LinearArena() noexcept = default;

        constexpr LinearArena(void* buffer, size_t capacity, ArenaFallbackFn fallback = nullptr) noexcept
            : m_cursor(static_cast<uint8_t*>(buffer))
            , m_limit(static_cast<uint8_t*>(buffer) + capacity)
            , m_base(static_cast<uint8_t*>(buffer))
            , m_fallback(fallback) { }

        // 用于绑定或重绑缓冲区，支持在运行时动态切换缓冲区
        ID_FORCEINLINE void init(void* buffer, size_t capacity, ArenaFallbackFn fallback = nullptr) noexcept
        {
            m_cursor = static_cast<uint8_t*>(buffer);
            m_limit  = static_cast<uint8_t*>(buffer) + capacity;
            m_base   = static_cast<uint8_t*>(buffer);
            m_fallback = fallback;
        }

        // 分配指定大小和对齐的内存块，返回指向分配内存的指针；若缓冲区不足则触发回调函数
        ID_FORCEINLINE void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept
        {
            // 计算对齐后的地址
            uintptr_t current = reinterpret_cast<uintptr_t>(m_cursor);
            uintptr_t aligned = (current + alignment - 1) & ~(alignment - 1);
            uint8_t* next = reinterpret_cast<uint8_t*>(aligned + size);

            // 检查是否超出缓冲区限制
            if (next > m_limit) [[unlikely]]
            {
                if (m_fallback)
                {
                    m_fallback(size, alignment);
                }
                return nullptr; // 分配失败，返回 nullptr
            }

            m_cursor = next; // 更新游标
            return reinterpret_cast<void*>(aligned); // 返回对齐后的地址
        }

        // 类型安全的模板分配辅助函数
        template <typename T>
        ID_FORCEINLINE T* allocate_array(size_t count = 1) noexcept
        {
            return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
        }

        // 重置分配器，将游标回退到缓冲区起始位置，允许重新使用整个缓冲区
        ID_FORCEINLINE void reset() noexcept
        {
            m_cursor = m_base;
        }

        ID_FORCEINLINE bool is_initialized() const noexcept
        {
            return m_base != nullptr && m_limit != nullptr;
        }

        // 获取缓冲区的总容量
        ID_FORCEINLINE size_t capacity() const noexcept
        {
            return static_cast<size_t>(m_limit - m_base);
        }

        // 获取已使用的字节数
        ID_FORCEINLINE size_t used_bytes() const noexcept
        {
            return static_cast<size_t>(m_cursor - m_base);
        }

    private:
        uint8_t*            m_cursor    = nullptr;          // 游标，指向当前可分配的内存位置
        uint8_t*            m_limit     = nullptr;          // 下限，指向缓冲区末尾，防止越界访问
        uint8_t*            m_base      = nullptr;          // 基址，指向缓冲区起始位置，用于整体重置
        ArenaFallbackFn     m_fallback  = nullptr;          // 设置回调函数，当缓冲区不足时触发回调，可用于日志、异常或动态分配等处理
    };
} // namespace ID