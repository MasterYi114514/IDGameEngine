#pragma once

#include "IDpch.hpp"

#include <span>

#include "Scene/Component/Component.hpp"

namespace ID
{
    /**
     *  @brief 实体 ID 别名，与 GameObject::ID（uint32_t）同型
     *
     *  ComponentPool / ComponentRegistry 只依赖此别名而不包含 GameObject.hpp，
     *  避免把 GameObject 的重依赖（TransformComponent / 序列化等）拖进池的实现。
     */
    using EntityID = uint32_t;

    /// 无效实体 ID，与 GameObject::INVALID_ID 同值
    inline constexpr EntityID INVALID_ENTITY_ID = static_cast<EntityID>(-1);

    /**
     *  @brief 组件池的类型擦除基类
     *
     *  ComponentRegistry 以 IComponentPool* 数组统一持有各类型池，
     *  池索引沿用 Component::get_static_type_id<T>() 的 consteval TypeID
     *  （编译期确定、跨 DLL 稳定，不引入任何运行时类型计数器）。
     */
    class IComponentPool
    {
    public:
        virtual ~IComponentPool() = default;

        /// owner 是否持有本池类型的组件
        virtual bool   has(EntityID owner) const = 0;

        /// 销毁 owner 的组件（内部先 on_detach 再搬移，见 ComponentPool::erase）
        virtual void   erase(EntityID owner) = 0;

        /// dense 数组中的组件数量
        virtual size_t size() const = 0;

        /// 按 GameObject 容量扩容稀疏数组（create_game_object 时调用）
        virtual void   reserve_for_game_objects(size_t capacity) = 0;

        /// 基类指针访问 owner 的组件（类型擦除遍历用），不存在返回 nullptr
        virtual Component*       get_component_base(EntityID owner)       = 0;
        virtual const Component* get_component_base(EntityID owner) const = 0;
    };

    /**
     *  @brief 单一组件类型的连续存储池（sparse set）
     *
     *  存储：m_components（dense，按值连续存储，缓存友好）
     *        m_owners    （dense，与 m_components 同下标，记录归属 GO）
     *        m_sparse    （sparse[go_id] → dense 下标，NULL_INDEX 表示无）
     *
     *  使用规矩（违反会导致悬垂引用 / 迭代器失效）：
     *  1. 组件引用 / 指针禁止跨帧缓存，禁止跨越任何 emplace / erase 持有——
     *     池的搬移（swap-and-pop）会使引用失效，需要时按 EntityID 重新查询；
     *  2. 遍历池期间禁止调用 emplace / erase（会搬动 dense 数组）。
     *     遍历请通过 IterationGuard 进入，Debug 下结构性修改会触发断言；
     *  3. erase 内部先对被删组件调用 on_detach()（释放 RigidBodyID / AudioSourceID
     *     等外部句柄），然后才搬移——顺序不可颠倒，否则句柄已被 move 走，
     *     物理世界 / 音频引擎会残留孤儿对象。
     *
     *  @tparam T 具体组件类型，必须继承 Component
     */
    template<typename T>
    class ComponentPool : public IComponentPool
    {
        static_assert(std::is_base_of_v<Component, T>, "ComponentPool 的模板参数必须是 Component 的子类");

    public:
        /// m_sparse 的"无组件"哨兵值
        static constexpr uint32_t NULL_INDEX = static_cast<uint32_t>(-1);

        /**
         *  @brief 遍历守卫（RAII）
         *
         *  进入池遍历前构造，离开作用域自动解除；遍历期间 emplace / erase
         *  会触发 Debug 断言（迭代器失效防护）。
         */
        class IterationGuard
        {
        public:
            explicit IterationGuard(ComponentPool& pool) : m_pool(pool)
            {
                assert(!m_pool.m_iterating && "禁止嵌套进入同一组件池的遍历");
                m_pool.m_iterating = true;
            }

            IterationGuard(const IterationGuard&)            = delete;
            IterationGuard& operator=(const IterationGuard&) = delete;

            ~IterationGuard() { m_pool.m_iterating = false; }

        private:
            ComponentPool& m_pool;
        };

    public:
        /**
         *  @brief 在池尾构造一个组件并登记到 owner 名下
         *  @param owner 归属 GameObject 的 ID
         *  @param args  透传给 T 构造函数的参数
         *  @return 新构造组件的引用
         *
         *  前置：owner 尚未持有本类型组件（单实例约束由 sparse set 天然保证，
         *  Debug 下以断言防护）。on_attach 由调用方（GameObject::add_component）执行，
         *  与旧实现"先构造、后 attach"的顺序保持一致。
         */
        template<typename... Args>
        T& emplace(EntityID owner, Args&&... args)
        {
            assert(!m_iterating && "emplace 禁止在池遍历期间调用（dense 数组搬移会使迭代失效）");
            assert(!has(owner) && "该 GameObject 已持有本类型组件（单实例约束）");

            if (owner >= m_sparse.size())
                m_sparse.resize(static_cast<size_t>(owner) + 1, NULL_INDEX);

            T& comp = m_components.emplace_back(std::forward<Args>(args)...);
            m_owners.push_back(owner);
            m_sparse[owner] = static_cast<uint32_t>(m_components.size() - 1);
            return comp;
        }

        /// 查询 owner 的组件，不存在返回 nullptr
        T* find(EntityID owner)
        {
            const uint32_t dense = sparse_index(owner);
            return dense == NULL_INDEX ? nullptr : &m_components[dense];
        }

        /// 查询 owner 的组件（const 重载），不存在返回 nullptr
        const T* find(EntityID owner) const
        {
            const uint32_t dense = sparse_index(owner);
            return dense == NULL_INDEX ? nullptr : &m_components[dense];
        }

        bool has(EntityID owner) const override
        {
            return sparse_index(owner) != NULL_INDEX;
        }

        /**
         *  @brief 销毁 owner 的组件
         *
         *  固定顺序（不可调整）：
         *  ① 对被删组件调用 on_detach()（释放物理 / 音频等外部句柄）
         *  ② 尾部组件 move 覆盖洞位（swap-and-pop）
         *  ③ 更新被移动组件的 m_sparse 条目
         *  ④ components / owners 双数组 pop_back
         *  ⑤ m_sparse[owner] = NULL_INDEX
         */
        void erase(EntityID owner) override
        {
            assert(!m_iterating && "erase 禁止在池遍历期间调用（dense 数组搬移会使迭代失效）");

            const uint32_t dense = sparse_index(owner);
            if (dense == NULL_INDEX)
                return;

            // ① on_detach 必须在任何搬移之前
            m_components[dense].on_detach();

            const uint32_t last = static_cast<uint32_t>(m_components.size()) - 1;
            if (dense != last)
            {
                // ② ③ 尾元素覆盖洞位并修正其稀疏映射
                m_components[dense] = std::move(m_components.back());
                m_owners[dense]     = m_owners.back();
                m_sparse[m_owners[dense]] = dense;
            }

            // ④ ⑤
            m_components.pop_back();
            m_owners.pop_back();
            m_sparse[owner] = NULL_INDEX;
        }

        /// dense 数组中的组件数量
        size_t size() const override { return m_components.size(); }

        /// 稀疏数组按 GO 容量扩容，只增不减
        void reserve_for_game_objects(size_t capacity) override
        {
            if (capacity > m_sparse.size())
                m_sparse.resize(capacity, NULL_INDEX);
        }

        /// dense 组件数组（下标与 owners() 一一对应）
        std::span<T>       components()          { return m_components; }
        std::span<const T> components() const    { return m_components; }

        /// dense 归属 GO 数组（下标与 components() 一一对应）
        std::span<const EntityID> owners() const { return m_owners; }

        Component*       get_component_base(EntityID owner) override       { return find(owner); }
        const Component* get_component_base(EntityID owner) const override { return find(owner); }

    private:
        /// owner → dense 下标，越界或无组件返回 NULL_INDEX
        uint32_t sparse_index(EntityID owner) const
        {
            return owner < m_sparse.size() ? m_sparse[owner] : NULL_INDEX;
        }

        std::vector<T>        m_components;   // dense：组件按值连续存储
        std::vector<EntityID> m_owners;       // dense：m_components[i] 的归属 GO
        std::vector<uint32_t> m_sparse;       // sparse：go_id → dense 下标
        bool                  m_iterating = false;    // 遍历防护标志（坑 B）
    };
} // namespace ID
