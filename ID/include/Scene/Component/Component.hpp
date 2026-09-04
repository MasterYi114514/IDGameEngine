#pragma once

#include "IDpch.hpp"
#include "IDJson.hpp"

namespace ID
{
    class GameObject;
    class Event;

    // ── 组件类型注册表 ──────────────────────────────────────────────
    // 新增组件类型时：1) 在此前向声明  2) 把类型追加到 get_static_type_id 的类型列表尾部
    class TransformComponent;
    class MeshRendererComponent;
    class LightComponent;
    class RigidBodyComponent;
    class AudioSourceComponent;
    class AudioListenerComponent;

    namespace detail
    {
        /**
         *  @brief 编译期计算类型 T 在类型列表中的位置（从 1 开始）
         *  @return 位置索引；0 表示 T 不在列表中（未登记）
         *
         *  纯编译期计算、无任何运行时状态：同一类型在任何编译单元 / DLL / EXE
         *  中都得到相同的 type_id，彻底避免跨模块计数器分叉导致的 TypeID 串台。
         */
        template<typename T, typename... Ts>
        consteval std::size_t type_index()
        {
            std::size_t idx = 0;
            std::size_t i   = 0;
            ((++i, (std::is_same_v<T, Ts> ? (idx = i) : 0)), ...);
            return idx;
        }
    } // namespace detail

    /**
     *  @brief 组件基类
     *
     *  组件由 Scene 的 ComponentRegistry 按类型池化持有（ComponentPool<T>），
     *  不再依附 GameObject 内部链表；生命周期与释放顺序见 ComponentPool.hpp。
     */
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

        bool is_active() const { return m_is_active; }
        void make_inactive() { m_is_active = false; }

        /**
         *  @brief 尝试激活组件
         *
         *  基类实现无条件激活；需要外部资源的组件（如 MeshRendererComponent
         *  依赖有效的 Mesh/Material、AudioSourceComponent 依赖 AudioEngine 音源句柄）
         *  应重载此函数：资源无效时拒绝激活（保持 inactive 并记日志）。
         *  调用方可通过 is_active() 确认激活是否成功。
         */
        virtual void make_active() { m_is_active = true; }

        static constexpr bool s_allow_multiple = true;   // 组件默认允许在同一 GameObject 上挂载多个实例

    public:
        // static type id 的设计
        virtual TypeID get_type_id() const = 0;

        /**
         *  @brief 获取组件类型的全局唯一 TypeID（编译期确定，跨 DLL 稳定）
         *
         *  type_id = 组件类型在下方类型列表中的位置（从 1 开始递增），
         *  由 consteval 在编译期计算，不依赖任何运行时状态。
         */
        template<typename ComponentType>
        static constexpr TypeID get_static_type_id()
        {
            static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");
            constexpr std::size_t idx = detail::type_index<ComponentType,
                TransformComponent, MeshRendererComponent, LightComponent,
                RigidBodyComponent, AudioSourceComponent, AudioListenerComponent>();
            static_assert(idx != 0,
                "组件类型未登记：请在前向声明区添加该类声明，并追加到类型列表中");
            return static_cast<TypeID>(idx);
        }

    public:
        // 获取组件的名称，必须与 ComponentFactory 中注册的名称一致
        virtual std::string get_component_type_name() const = 0;

    protected:
        GameObject* m_owner = nullptr;              // 组件所属的 GameObject

        bool m_is_active = false;                    // 组件是否激活，新组件默认不激活
    };
} // namespace ID
