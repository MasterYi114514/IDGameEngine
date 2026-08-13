#pragma once

#include "IDPhysics.hpp"
#include "Scene/System/System.hpp"
#include "Scene/Component/RigidBodyComponent.hpp"

namespace ID
{
    class ID_API PhysicsSystem : public System
    {
    public:
        PhysicsSystem() = default;
        ~PhysicsSystem() = default;

        PhysicsSystem(const PhysicsSystem&) = delete;
        PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    public:
        void on_attach(Scene* scene = nullptr) override;
        void on_detach() override;
        void on_update(Timestep ts) override;
        void on_event(Event& event) override;

    public:
        void set_gravity(const Vec3& gravity);
        Vec3 get_gravity() const;

        PhysicsWorld& get_physics_world() { return m_physics_world; }
        const PhysicsWorld& get_physics_world() const { return m_physics_world; }

        /**
         *  同步刚体的位姿与 TransformComponent
         *  对于 static 与 kinematic 类型的刚体，TransformComponent 的位姿会覆盖刚体的位姿
         *  对于 dynamic 类型的刚体，刚体的位姿会覆盖 TransformComponent 的位姿
         */
        void sync_pose(RigidBodyID rigid_body);
        
    private:
        /// 收集场景中所有 RigidBodyComponent，创建/更新刚体
        void sync_rigid_bodies();

        /// Static / Kinematic：推送 Transform → PhysicsWorld
        void push_transforms();

        /// Dynamic：拉回 PhysicsWorld → TransformComponent
        void pull_transforms();

    private:
        PhysicsWorld    m_physics_world;
        Scene*          m_scene = nullptr;
    };
} // namespace ID