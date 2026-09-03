#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "Scene/Component/ComponentPool.hpp"
#include "Scene/Component/ComponentRegistry.hpp"
#include "Scene/Component/TransformComponent.hpp"

namespace ID
{
    class Scene;

    /**
     *  @brief 场景实体：名称 / 层级 / 激活状态，不持有组件
     *
     *  ⚠️ 组件存储已池化（Scene 的 ComponentRegistry）：组件由对应类型的
     *  ComponentPool<T> 按值连续存储，本类仅提供经 Registry 的访问入口。
     *
     *  坑 C 规矩（必须遵守）：
     *  - 组件引用 / 指针（add/get/remove_component 返回值）禁止跨帧缓存，
     *    禁止跨越任何 add/remove_component 调用持有——池的 swap-and-pop
     *    搬移会使引用失效；需要时按 GameObject::ID 重新 get_component。
     */
    class ID_API GameObject
    {
    public:
        using ID = uint32_t;
        static constexpr ID INVALID_ID = static_cast<ID>(-1);

    public:
        GameObject() = delete;
        ~GameObject();

        // 禁止拷贝
        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;

        // 允许移动
        GameObject(GameObject&&) = default;
        GameObject& operator=(GameObject&&) = default;

    private:
        friend class Scene;
        GameObject(Scene* scene, ID id, const std::string& name = "GameObject");

    public:
        // 基本访问 API
        ID                          get_id() const { return m_id; }
        const std::string&          get_name() const { return m_name; }
        void                        set_name(const std::string& name) { m_name = name; }

        bool                        is_active() const { return m_is_active; }
        void                        set_active(bool active) { m_is_active = active; }

        Scene*                      get_scene() const { return m_scene; }

        /// 访问所属场景的组件池注册中心（实现在 .cpp：头文件内 Scene 仅为前向声明）
        ComponentRegistry&          get_registry();
        const ComponentRegistry&    get_registry() const;

    public:
        // 父子层级处理
        void                        set_parent(ID parent_id);       // 设置父 GameObject
        GameObject&                 get_parent() const;
        ID                          get_parent_id() const { return m_parent_id; }

        std::vector<ID>&            get_children() { return m_children; }
        const std::vector<ID>&      get_children() const { return m_children; }

    public:
        /**
         *  @brief 挂载一个 Component 到 GameObject 上
         *  @return 返回新挂载的 Component 的引用
         *
         *  单实例约束：池化后每 GO 每类型至多一个组件，已存在时直接返回
         *  已有组件的引用（AudioSourceComponent 原允许多实例，池化已决策
         *  退化为单实例，见迁移计划 R2）。
         */
        template<typename ComponentType, typename... Args>
        ComponentType& add_component(Args&&... args);

        /**
         *  @brief 收纳一个预构造的组件（反序列化路径专用）
         *
         *  保持旧顺序：调用方先 component->deserialize(json)，本函数再入池
         *  并 on_attach（AudioSourceComponent 依赖此顺序传递 pending 激活状态）。
         *  已存在同类型组件时丢弃传入对象（未 attach、无资源需释放）。
         */
        template<typename ComponentType>
        ComponentType& adopt_component(std::unique_ptr<ComponentType> component);

        /**
         *  @brief 获取指定类型的第一个 Component
         *  @return 返回第一个指定类型的 Component，如果未找到则返回 nullptr
         */
        template<typename ComponentType>
        ComponentType* get_component();

        template<typename ComponentType>
        const ComponentType* get_component() const;

        template<typename ComponentType>
        bool has_component() const;

        template<typename ComponentType>
        void remove_component();

    public:
        void on_update(Timestep ts);
        void on_event(Event& event);

    private:
        Scene*      m_scene = nullptr;          // 所属场景
        ID          m_id = INVALID_ID;          // GameObject ID
        std::string m_name;
        bool        m_is_active = false;         // 是否激活（参与更新和渲染）

        ID              m_parent_id = INVALID_ID;       // 父 GameObject ID
        std::vector<ID> m_children;                     // 子 GameObject ID 列表

        static GameObject default_game_object;          // 默认的 GameObject，用于返回引用，避免返回空指针

    public:
        // 将 GameObject 序列化为 Json 树
        Json serialize(ArenaID arena) const;

        // 从 Json 树反序列化 GameObject 数据
        void deserialize(const Json& json);
    };

    // GameObject 的模版函数实现 ------------------------------------------------------------------------------
    template<typename ComponentType, typename... Args>
    ComponentType& GameObject::add_component(Args&&... args)
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        ComponentRegistry& registry = get_registry();

        // 单实例检查：池化后每 GO 每类型至多一个，已存在则直接返回已有组件的引用
        if (ComponentType* existing = registry.get<ComponentType>(m_id))
        {
            return *existing;
        }

        // 前置组件检查：若该 GO 没有前置组件，则自动添加
        // （TransformComponent 无资源依赖，自动添加后直接激活）
        if constexpr (std::same_as<ComponentType, RigidBodyComponent>)
        {
            if(!registry.has<TransformComponent>(m_id))
            {
                add_component<TransformComponent>().make_active();
            }
        }

        if constexpr (std::same_as<ComponentType, AudioSourceComponent>)
        {
            if(!registry.has<TransformComponent>(m_id))
            {
                add_component<TransformComponent>().make_active();
            }
        }

        if constexpr (std::same_as<ComponentType, AudioListenerComponent>)
        {
            if(!registry.has<TransformComponent>(m_id))
            {
                add_component<TransformComponent>().make_active();
            }
        }

        if constexpr (std::same_as<ComponentType, MeshRendererComponent>)
        {
            if(!registry.has<TransformComponent>(m_id))
            {
                add_component<TransformComponent>().make_active();
            }
        }

        // 先构造进池 → 再 on_attach（与旧实现 push 后 attach 的顺序一致）
        ComponentType& ref = registry.emplace<ComponentType>(m_id, std::forward<Args>(args)...);
        ref.on_attach(this);
        return ref;
    }

    template<typename ComponentType>
    ComponentType& GameObject::adopt_component(std::unique_ptr<ComponentType> component)
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        ComponentRegistry& registry = get_registry();

        // 单实例约束：已有则丢弃传入对象（未 attach、无资源需释放），返回已有组件
        if (ComponentType* existing = registry.get<ComponentType>(m_id))
        {
            return *existing;
        }

        // 组件按值入池：依赖隐式拷贝构造（组件析构不管资源，源 unique_ptr 随后无害析构）
        ComponentType& ref = registry.emplace<ComponentType>(m_id, std::move(*component));
        ref.on_attach(this);
        return ref;
    }

    template<typename ComponentType>
    ComponentType* GameObject::get_component()
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        return get_registry().get<ComponentType>(m_id);
    }

    template<typename ComponentType>
    const ComponentType* GameObject::get_component() const
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        return get_registry().get<ComponentType>(m_id);
    }

    template<typename ComponentType>
    bool GameObject::has_component() const
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        return get_registry().has<ComponentType>(m_id);
    }

    template<typename ComponentType>
    void GameObject::remove_component()
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        // 池 erase 内部先 on_detach（释放物理/音频句柄）再搬移，
        // 修复旧实现“移除 RigidBody 组件不释放物理刚体”的历史隐患
        get_registry().erase<ComponentType>(m_id);
    }

} // namespace ID