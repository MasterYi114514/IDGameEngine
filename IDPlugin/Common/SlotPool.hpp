#pragma once

#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

/**
 *  按下标索引的对象池：`create` 返回稳定下标，`destroy` 释放槽位并把下标回收复用。
 *  
 *  不变量：
 *  1. `m_pool` 只会增长，永不因 `destroy` 收缩；被销毁的槽位通过 `T::destroy()` 自行清空持有，
 *     因此"已销毁"由 `T` 自身表达，本池不保存存活位图。
 *  2. 同一个下标在归还空闲表之前只允许 `destroy` 一次；重复归还同一下标会污染空闲表，
 *     调用方必须先判定槽位存活（由 `T` 提供语义），本池不做该判定。
 *  3. `create` 复用空闲下标时以移动赋值覆盖旧内容（此时旧内容已由 `destroy()` 清理）。
 *  
 *  取舍：
 *  - 空闲表用 `std::vector` 直接做栈，不用 `std::stack`（避免 deque 的额外间接与块分配）。
 *  - 槽位下标即外部句柄，因此数组扩容会使取出的元素地址失效；本池不承诺元素地址稳定。
 *  - 槽位数量上限为 `UintType` 可表示的最大值减一（最大值留给无效句柄），超出范围不做检查。
 *  - 不提供任何线程安全承诺：并发由持有者（例如 LoggerManager 的互斥锁）覆盖。
 */

namespace ID::Common
{
    template <std::unsigned_integral UintType, typename T>
    class SlotPool
    {
        static_assert(requires(T& slot) { { slot.destroy() } noexcept; },
                      "SlotPool requires a noexcept destroy() member on T");

    public:
        /**
         *  @brief 取得一个槽位并放入对象
         *  @param obj 被移入槽位的对象；调用后 obj 处于有效但未指定状态
         *  @return 槽位下标，可作为句柄保存
         *  @note 可能分配（空闲表为空时扩容 `m_pool`）；分配失败以异常传播给调用方
         */
        UintType create(T&& obj);

        /**
         *  @brief 销毁槽位内容并回收下标
         *  @param index 槽位下标；越界时无操作
         *  @note 先调用 `T::destroy()` 完成对象自身清理，再把下标压入空闲表
         *  @note 可能分配（空闲表扩容）；分配失败时吞掉异常、丢弃该下标（槽位不复用），
         *        对象已清理因此不会泄漏，本函数也不会向调用方抛出
         */
        void destroy(UintType index);

        /**
         *  @brief 按下标取槽位
         *  @return 越界时返回 nullptr；不判定槽位是否存活
         */
        T* try_get(UintType index) noexcept;
        const T* try_get(UintType index) const noexcept;

        std::size_t size() const noexcept { return m_pool.size(); }
        bool empty() const noexcept { return m_pool.empty(); }

        /**
         *  @brief 销毁全部槽位内容并清空池（下标从 0 重新开始）
         *  @note 逐个调用 `T::destroy()`；要求该调用可重复（对已销毁槽位是空操作）
         */
        void clear() noexcept;

    private:
        std::vector<T>          m_pool;             // 槽位数组，只增长
        std::vector<UintType>   m_freed_indices;    // 空闲下标栈，尾部为最近归还项
    };

    template <std::unsigned_integral UintType, typename T>
    UintType SlotPool<UintType, T>::create(T&& obj)
    {
        if (!m_freed_indices.empty()) [[likely]]
        {
            const UintType index = m_freed_indices.back();
            m_freed_indices.pop_back();
            m_pool[index] = std::move(obj);
            return index;
        }

        m_pool.emplace_back(std::move(obj));
        return static_cast<UintType>(m_pool.size() - 1);
    }

    template <std::unsigned_integral UintType, typename T>
    void SlotPool<UintType, T>::destroy(UintType index)
    {
        if (static_cast<std::size_t>(index) >= m_pool.size()) [[unlikely]]
        {
            return;
        }

        m_pool[index].destroy();

        try
        {
            m_freed_indices.push_back(index);
        }
        catch (...)
        {
            // 空闲表扩容失败：丢弃该下标，槽位不复用。对象已由上面的 destroy() 清理，不会泄漏；
            // 本函数因此不会向 noexcept 调用方抛出（否则会直接 terminate）。
        }
    }

    template <std::unsigned_integral UintType, typename T>
    T* SlotPool<UintType, T>::try_get(UintType index) noexcept
    {
        if (static_cast<std::size_t>(index) >= m_pool.size()) [[unlikely]]
        {
            return nullptr;
        }

        return &m_pool[index];
    }

    template <std::unsigned_integral UintType, typename T>
    const T* SlotPool<UintType, T>::try_get(UintType index) const noexcept
    {
        if (static_cast<std::size_t>(index) >= m_pool.size()) [[unlikely]]
        {
            return nullptr;
        }

        return &m_pool[index];
    }

    template <std::unsigned_integral UintType, typename T>
    void SlotPool<UintType, T>::clear() noexcept
    {
        for (T& slot : m_pool)
        {
            slot.destroy();
        }

        m_pool.clear();
        m_freed_indices.clear();
    }
} // namespace ID::Common
