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

    /**
     *  @brief 线性内存分配器（LinearArena），当缓冲区不足时可触发回调函数
     *  @note 1. 该分配器在给定的连续内存缓冲区上进行分配，每次分配操作仅为单次指针加法，性能高，适用于需要快速分配和释放的场景。分配器不支持单个内存块的释放，只有整体重置功能。
     *  @note 2. 该分配器不具有线程安全性，调用方需要自行确保在多线程环境下的使用是安全的。
     */
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

        /**
         *  @brief 分配指定类型 T 的数组，返回指向数组首元素的指针
         *  @param count 数组元素个数，默认为 1
         *  @return 指向数组首元素的指针；若缓冲区不足则触发回调函数并返回 nullptr
         */
        template <typename T>
        ID_FORCEINLINE T* allocate_array(size_t count = 1) noexcept
        {
            return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
        }

        /**
         *  @brief 重置分配器
         *  @note 该操作只会将游标重置到缓冲区起始位置，调用方需要确保在重置后不再使用之前分配的内存。
         */
        ID_FORCEINLINE void reset() noexcept
        {
            m_cursor = m_base;
        }

        /**
         *  @brief 检查分配器是否已初始化
         *  @return 如果缓冲区已绑定且游标和限制指针有效，则返回 true；否则返回 false
         */
        ID_FORCEINLINE bool is_initialized() const noexcept
        {
            return m_base != nullptr && m_limit != nullptr;
        }

        /**
         *  @brief 获取分配器的总容量
         *  @return 返回缓冲区的总字节数
         */
        ID_FORCEINLINE size_t capacity() const noexcept
        {
            return static_cast<size_t>(m_limit - m_base);
        }

        /**
         *  @brief 获取已使用的字节数
         *  @return 返回从缓冲区起始位置到当前游标的字节数，即已分配的内存总量
         */
        ID_FORCEINLINE size_t used_bytes() const noexcept
        {
            return static_cast<size_t>(m_cursor - m_base);
        }

        /**
         *  @brief 获取剩余可用的字节数
         *  @return 返回从当前游标到缓冲区末尾的字节数，即剩余可分配的内存总量
         */
        ID_FORCEINLINE size_t remaining_bytes() const noexcept
        {
            return static_cast<size_t>(m_limit - m_cursor);
        }

    private:
        uint8_t*            m_cursor    = nullptr;          // 游标，指向当前可分配的内存位置
        uint8_t*            m_limit     = nullptr;          // 下限，指向缓冲区末尾，防止越界访问
        uint8_t*            m_base      = nullptr;          // 基址，指向缓冲区起始位置，用于整体重置
        ArenaFallbackFn     m_fallback  = nullptr;          // 设置回调函数，当缓冲区不足时触发回调，可用于日志、异常或动态分配等处理
    };
} // namespace ID