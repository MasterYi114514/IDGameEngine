#pragma once

#include "IDpch.hpp"
#include "IDJson.hpp"

namespace ID
{
    class GameObject;
    class Event;

    class ID_API Component : public SerializableBase
    {
    public:
        using TypeID = uint32_t;
        static constexpr TypeID INVALID_ID = static_cast<TypeID>(-1);

    public:
        Component() { }
        virtual ~Component() = default;         // 不参与释放资源

        // 子类必须显示调用父类的 on_attach()
        virtual void on_attach(GameObject* owner) { m_owner = owner; }
        virtual void on_detach() { }
        virtual void on_update(Timestep ts) { }
        virtual void on_event(Event& event) { }

        GameObject* get_owner() const { return m_owner; }

    public:
        // static type id 的设计
        virtual TypeID get_type_id() const = 0;

        template<typename ComponentType>
        static TypeID get_static_type_id()
        {
            static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");
            static TypeID type_id = s_next_type_id++;
            return type_id;
        }

    public:
        // 获取组件的名称，必须与 ComponentFactory 中注册的名称一致
        virtual std::string get_component_type_name() const = 0;

    protected:
        GameObject* m_owner = nullptr;              // 组件所属的 GameObject
        static inline TypeID s_next_type_id = 0;    // 为每种组件类型分配一个唯一的 ID
    };
} // namespace ID