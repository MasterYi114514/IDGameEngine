#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "Scene/Component/TransformComponent.hpp"

namespace ID
{
    class Scene;

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

        TransformComponent&         get_transform() { return m_transform; }
        const TransformComponent&   get_transform() const { return m_transform; }

    public:
        // 父子层级处理
        Mat4                        get_world_matrix() const;       // 委托给 TransformComponent 获取世界矩阵
        void                        set_parent(ID parent_id);       // 设置父 GameObject
        GameObject&                 get_parent() const;
        ID                          get_parent_id() const { return m_parent_id; }

        std::vector<ID>&            get_children() { return m_children; }
        const std::vector<ID>&      get_children() const { return m_children; }

    public:
        /**
         *  @brief 挂载一个 Component 到 GameObject 上
         *  @return 返回新挂载的 Component 的引用
         */
        template<typename ComponentType, typename... Args>
        ComponentType& add_component(Args&&... args);

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
        bool        m_is_active = true;         // 是否激活（参与更新和渲染）

        TransformComponent        m_transform;          // 每个 GameObject 都有一个 TransformComponent

        using ComponentPtr = std::unique_ptr<Component>;
        std::vector<ComponentPtr> m_components;
        std::unordered_map<Component::TypeID, Component*> m_component_index;  // O(1) 组件快速查找

        ID              m_parent_id = INVALID_ID;       // 父 GameObject ID
        std::vector<ID> m_children;                     // 子 GameObject ID 列表

        static GameObject default_game_object;          // 默认的 GameObject，用于返回引用，避免返回空指针
    };

    // GameObject 的模版函数实现 ------------------------------------------------------------------------------
    template<typename ComponentType, typename... Args>
    ComponentType& GameObject::add_component(Args&&... args)
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        auto component = std::make_unique<ComponentType>(std::forward<Args>(args)...);
        component->on_attach(this);
        ComponentType& ref = *component;

        // 索引表：该类型尚无记录时写入，保证 get_component O(1)
        Component::TypeID type_id = Component::get_static_type_id<ComponentType>();
        if (m_component_index.find(type_id) == m_component_index.end())
        {
            m_component_index[type_id] = component.get();
        }

        m_components.push_back(std::move(component));
        return ref;
    }

    template<typename ComponentType>
    ComponentType* GameObject::get_component()
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        Component::TypeID type_id = Component::get_static_type_id<ComponentType>();
        auto it = m_component_index.find(type_id);
        if (it != m_component_index.end())
        {
            return static_cast<ComponentType*>(it->second);
        }
        return nullptr;
    }

    template<typename ComponentType>
    const ComponentType* GameObject::get_component() const
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        Component::TypeID type_id = Component::get_static_type_id<ComponentType>();
        auto it = m_component_index.find(type_id);
        if (it != m_component_index.end())
        {
            return static_cast<const ComponentType*>(it->second);
        }
        return nullptr;
    }

    template<typename ComponentType>
    bool GameObject::has_component() const
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        Component::TypeID type_id = Component::get_static_type_id<ComponentType>();
        return m_component_index.find(type_id) != m_component_index.end();
    }

    template<typename ComponentType>
    void GameObject::remove_component()
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        Component::TypeID target_type = Component::get_static_type_id<ComponentType>();

        // 找到第一个该类型的组件（索引始终指向第一个同类型组件）
        auto it = std::find_if(m_components.begin(), m_components.end(),
            [target_type](const std::unique_ptr<Component>& comp)
            {
                return comp->get_type_id() == target_type;
            });

        if (it == m_components.end()) return;   // 没有该类型组件

        // 如果删除的是索引指向的组件，把索引更新到下一个同类型组件
        auto index_it = m_component_index.find(target_type);
        if (index_it != m_component_index.end() && index_it->second == it->get())
        {
            Component* next = nullptr;
            for (auto next_it = std::next(it); next_it != m_components.end(); ++next_it)
            {
                if ((*next_it)->get_type_id() == target_type)
                {
                    next = next_it->get();
                    break;
                }
            }
            if (next)
                m_component_index[target_type] = next;
            else
                m_component_index.erase(target_type);
        }

        m_components.erase(it);
    }

} // namespace ID