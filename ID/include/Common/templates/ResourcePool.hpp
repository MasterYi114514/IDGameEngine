#pragma once

#include <cstdint>
#include <concepts>
#include <vector>
#include <unordered_set>

namespace ID
{
    /**
     *  @brief 一个复用空闲槽位的资源池模板类
     *  @note 资源类本身必须可以移动
     *  @note 资源类本身必须提供 destroy() 方法，用于在资源被销毁时执行必要的
     */
    template <std::unsigned_integral UintType, typename T>
    struct ResourcePool
    {
        static_assert(std::is_move_assignable_v<T>, "T must be move assignable");
        static_assert(requires(T t) { t.destroy(); }, "T must have a destroy() method");
        
        std::vector<T>                                  m_pool;
        std::vector<bool>                               m_is_active;
        std::stack<UintType, std::vector<UintType>>     m_free_indices;

        static UintType invalid_index() { return static_cast<UintType>(-1); }   // 无效索引

        /**
         *  @brief 分配一个新的资源槽位，并将资源对象移动到该槽位
         *  @param res 要分配的资源对象
         *  @return 分配的资源索引，如果分配失败则返回无效索引
         */
        UintType allocate_slot(T&& res);

        /**
         *  @brief 销毁指定索引的资源
         *  @param index 要销毁的资源索引
         *  @return 如果索引有效且资源处于活跃状态，则返回 true；否则返回 false
         *  @note 销毁资源后，该索引将被标记为非活跃状态，并可被后续的 allocate_slot() 调用复用
         */
        bool destroy(UintType index);

        /**
         *  @brief 检查指定索引的资源是否处于活跃状态
         *  @param index 要检查的资源索引
         *  @return 如果索引有效且资源处于活跃状态，则返回 true；否则返回 false
         */
        bool is_active(UintType index) const { return m_is_active[index]; }

        /**
         *  @brief 预分配资源池的容量，减少 vector 的动态扩容的开销
         *  @param count 要预分配的容量
         */
        void reserve(UintType count) { m_pool.reserve(count); m_is_active.reserve(count); }
    };

    template <std::unsigned_integral UintType, typename T>
    UintType ResourcePool<UintType, T>::allocate_slot(T&& res)
    {
        UintType new_index{ invalid_index() };

        if (!m_free_indices.empty())
        {
            new_index = m_free_indices.top();
            m_free_indices.pop();

            m_pool[new_index] = std::move(res);
            m_is_active[new_index] = true;
        }
        else
        {
            new_index = static_cast<UintType>(m_pool.size());
            m_pool.emplace_back(std::move(res));
            m_is_active.emplace_back(true);
        }
        return new_index;
    }

    template <std::unsigned_integral UintType, typename T>
    bool ResourcePool<UintType, T>::destroy(UintType index)
    {
        if (index >= m_pool.size()) [[unlikely]]
        {
            return false;           // 索引越界，无法销毁
        }

        if (!m_is_active[index]) [[unlikely]]
        {
            return false;           // 资源已被销毁，无法再次销毁
        }

        m_pool[index].destroy();        // 调用 T 的 destroy() 方法销毁资源
        m_free_indices.push(index);     // 将索引加入空闲栈，供后续分配使用
        m_is_active[index] = false;     // 标记为非活跃状态

        return true;
    }
}