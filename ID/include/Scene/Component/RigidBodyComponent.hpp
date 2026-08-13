#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "IDPhysics.hpp"

namespace ID
{
    class ID_API RigidBodyComponent : public Component
    {
    public:
        RigidBodyComponent() = default;
        ~RigidBodyComponent() override = default;

        // Component 接口实现
        void on_attach(GameObject* owner) override;
        void on_detach() override;
        void on_update(Timestep ts) override;

        TypeID get_type_id() const override { return get_static_type_id<RigidBodyComponent>(); }
        std::string get_component_type_name() const override { return "RigidBodyComponent"; }
        bool allow_multiple() const override { return false; }  // 每个 GO 只允许一个刚体组件

    public:
        // 物理属性

        const RigidBodyCreateInfo& get_info() const { return m_info; }

        void set_mass(float mass);
        float get_mass() const;

        void set_type(RigidBodyType type);
        RigidBodyType get_type() const;
        bool is_static() const;
        bool is_dynamic() const;
        bool is_kinematic() const;

        // Trigger
        void set_trigger(bool trigger);
        bool is_trigger() const;

        // 线性阻尼
        void set_liner_damping(float damping);
        float get_linear_damping() const;

        // 角阻尼
        void set_angular_damping(float damping);
        float get_angular_damping() const;

        // 碰撞体形状
        void set_collider_shape(const ColliderShape& shape);
        const ColliderShape& get_collider_shape() const;

        // 物理材质
        void set_material(const PhysicsMaterial& material);
        const PhysicsMaterial& get_material() const;

        // 力与速度
        void apply_force(const Vec3& force);
        void apply_impulse(const Vec3& impulse);
        void apply_torque(const Vec3& torque);

        void set_linear_velocity(const Vec3& velocity);
        Vec3 get_linear_velocity() const;

    public:
        // 序列化
        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;

    private:
        friend class PhysicsSystem;

        RigidBodyCreateInfo m_info;
        RigidBodyID         m_rigid_body;
        PhysicsWorld*       m_world     = nullptr;       // 由 PhysicsSystem 在同步时设置
        bool                m_need_sync = true;          // 是否同步到 PhysicsWorld
    };
} // namespace ID