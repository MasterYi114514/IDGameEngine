#pragma once

#include "IDpch.hpp"

#include <array>

#include "Scene/Component/ComponentPool.hpp"

namespace ID
{
    /**
     *  @brief 组件注册中心：Scene 持有，按类型聚合所有 ComponentPool
     *
     *  池数组按 Component::get_static_type_id<T>()（consteval 编译期确定，
     *  跨 DLL 稳定）索引。池惰性构造：首次 pool<T>() 访问时才创建，
     *  本头文件因此无需包含任何具体组件头（由调用方保证模板实例化
     *  前已包含对应组件定义）。
     *
     *  使用规矩（继承自 ComponentPool 的坑 B / 坑 C）：
     *  - 组件引用 / 指针禁止跨帧缓存、禁止跨越任何 emplace / erase 持有；
     *  - 按池遍历期间禁止结构性修改（emplace / erase）。
     */
    class ID_API ComponentRegistry
    {
    public:
        /// 池数量，与 Component.hpp 前向声明区的组件类型列表保持同步
        /// （Transform / MeshRenderer / Light / RigidBody / AudioSource / AudioListener）
        static constexpr size_t s_pool_count = 6;

    public:
        ComponentRegistry() = default;
        ~ComponentRegistry() = default;

        // 禁止拷贝（持有池所有权）；允许默认移动（unique_ptr 转移）
        ComponentRegistry(const ComponentRegistry&)            = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;
        ComponentRegistry(ComponentRegistry&&)                 = default;
        ComponentRegistry& operator=(ComponentRegistry&&)      = default;

        /// 获取（或首次创建）T 类型的池；模板实例化要求调用方已包含 T 的定义
        template<typename T>
        ComponentPool<T>& pool()
        {
            static_assert(std::is_base_of_v<Component, T>, "T 必须是 Component 的子类");
            auto& slot = m_pools[pool_index<T>()];
            if (!slot)
                slot = std::make_unique<ComponentPool<T>>();
            return *static_cast<ComponentPool<T>*>(slot.get());
        }

        /// 查询 owner 的 T 组件（只读路径，不触发池构造），不存在返回 nullptr
        template<typename T>
        const T* get(EntityID owner) const
        {
            static_assert(std::is_base_of_v<Component, T>, "T 必须是 Component 的子类");
            const IComponentPool* slot = m_pools[pool_index<T>()].get();
            return slot ? static_cast<const ComponentPool<T>*>(slot)->find(owner) : nullptr;
        }

        /// 查询 owner 的 T 组件（写入路径），不存在返回 nullptr
        template<typename T>
        T* get(EntityID owner)
        {
            static_assert(std::is_base_of_v<Component, T>, "T 必须是 Component 的子类");
            IComponentPool* slot = m_pools[pool_index<T>()].get();
            return slot ? static_cast<ComponentPool<T>*>(slot)->find(owner) : nullptr;
        }

        /// owner 是否持有 T 组件
        template<typename T>
        bool has(EntityID owner) const
        {
            return get<T>(owner) != nullptr;
        }

        /// 在 T 池中构造组件（前置：!has<T>(owner)），透传构造参数，返回新组件引用
        template<typename T, typename... Args>
        T& emplace(EntityID owner, Args&&... args)
        {
            return pool<T>().emplace(owner, std::forward<Args>(args)...);
        }

        /// 销毁 owner 的 T 组件（内部先 on_detach 再搬移，见 ComponentPool::erase）
        template<typename T>
        void erase(EntityID owner)
        {
            IComponentPool* slot = m_pools[pool_index<T>()].get();
            if (slot)
                slot->erase(owner);
        }

        /// 稀疏数组按 GO 容量扩容，转发给所有已构造的池
        void reserve_for_game_objects(size_t capacity)
        {
            for (auto& slot : m_pools)
            {
                if (slot)
                    slot->reserve_for_game_objects(capacity);
            }
        }

        /// 销毁 owner 的全部组件（GO 销毁 / Scene 清理时逐池调用，触发各组件 on_detach）
        void erase_all_of(EntityID owner)
        {
            for (auto& slot : m_pools)
            {
                if (slot)
                    slot->erase(owner);
            }
        }

        /**
         *  @brief 类型擦除遍历 owner 的全部组件（serialize / on_update / on_event 用）
         *
         *  按 TypeID 升序（池声明顺序）逐池查询 owner 的组件并调用 fn(type_id, comp)。
         *  每池至多命中一个组件（sparse set 单实例）。fn 内部对组件的引用仅当次有效，
         *  禁止缓存（坑 C）；fn 期间禁止 emplace / erase（坑 B）。
         */
        template<typename Fn>
        void for_each_component_of(EntityID owner, Fn&& fn)
        {
            for (size_t i = 0; i < s_pool_count; ++i)
            {
                IComponentPool* slot = m_pools[i].get();
                if (!slot) continue;
                Component* comp = slot->get_component_base(owner);
                if (comp)
                    fn(static_cast<Component::TypeID>(i + 1), *comp);
            }
        }

        /// const 重载：fn 接收 const Component&
        template<typename Fn>
        void for_each_component_of(EntityID owner, Fn&& fn) const
        {
            for (size_t i = 0; i < s_pool_count; ++i)
            {
                const IComponentPool* slot = m_pools[i].get();
                if (!slot) continue;
                const Component* comp = slot->get_component_base(owner);
                if (comp)
                    fn(static_cast<Component::TypeID>(i + 1), *comp);
            }
        }

    private:
        /// T 的池数组下标（TypeID 从 1 开始，数组从 0 开始）
        template<typename T>
        static constexpr size_t pool_index()
        {
            return static_cast<size_t>(Component::get_static_type_id<T>()) - 1;
        }

        std::array<std::unique_ptr<IComponentPool>, s_pool_count> m_pools;
    };
} // namespace ID
