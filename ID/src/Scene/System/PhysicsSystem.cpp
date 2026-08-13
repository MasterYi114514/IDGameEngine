#include "Scene/System/PhysicsSystem.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/GameObject.hpp"

#include "Log/Log.hpp"

namespace ID
{
    // ========== 生命周期 ==========

    void PhysicsSystem::on_attach(Scene* scene)
    {
        m_scene = scene;
        ID_INFO("PhysicsSystem attached to scene '{}'", scene ? scene->get_name() : "null");
    }

    void PhysicsSystem::on_detach()
    {
        // PhysicsWorld 析构时会自动清理所有刚体
        m_scene = nullptr;
        ID_INFO("PhysicsSystem detached");
    }

    // ========== 每帧更新 ==========

    void PhysicsSystem::on_update(Timestep ts)
    {
        if (!m_scene) return;

        // 阶段 1：收集场景中所有 RigidBodyComponent，创建/更新刚体
        sync_rigid_bodies();

        // 阶段 2：推送 Static / Kinematic 的 Transform → PhysicsWorld
        push_transforms();

        // 阶段 3：步进物理模拟
        m_physics_world.step_simulation(ts.get_seconds());

        // 阶段 4：拉回 Dynamic 的 PhysicsWorld → TransformComponent
        pull_transforms();
    }

    void PhysicsSystem::on_event(Event& /*event*/)
    {
        // 当前无事件处理
    }

    // ========== 世界设置 ==========

    void PhysicsSystem::set_gravity(const Vec3& gravity)
    {
        m_physics_world.set_gravity(gravity);
    }

    Vec3 PhysicsSystem::get_gravity() const
    {
        return m_physics_world.get_gravity();
    }

    // ========== 位姿同步 ==========

    void PhysicsSystem::sync_pose(RigidBodyID rigid_body)
    {
        if (!m_physics_world.is_rigid_body_valid(rigid_body)) return;

        RigidBody& body = m_physics_world.get_rigid_body(rigid_body);

        // Static / Kinematic：推送 Transform → 物理引擎
        if (body.is_static() || body.is_kinematic())
        {
            // 需要从 Component 中找到对应的 TransformComponent
            auto go_ids = m_scene->find_game_objects_with_component<RigidBodyComponent>();
            for (GameObject::ID go_id : go_ids)
            {
                GameObject& go = m_scene->get_game_object(go_id);
                RigidBodyComponent* comp = go.get_component<RigidBodyComponent>();
                if (comp && comp->m_rigid_body == rigid_body)
                {
                    TransformComponent* transform = go.get_component<TransformComponent>();
                    if (transform)
                    {
                        body.set_transform(transform->get_position(), transform->get_orientation());
                    }
                    break;
                }
            }
        }
        // Dynamic：拉取 物理引擎 → Transform
        else if (body.is_dynamic())
        {
            auto go_ids = m_scene->find_game_objects_with_component<RigidBodyComponent>();
            for (GameObject::ID go_id : go_ids)
            {
                GameObject& go = m_scene->get_game_object(go_id);
                RigidBodyComponent* comp = go.get_component<RigidBodyComponent>();
                if (comp && comp->m_rigid_body == rigid_body)
                {
                    TransformComponent* transform = go.get_component<TransformComponent>();
                    if (transform)
                    {
                        transform->set_position(body.get_position());
                        transform->set_orientation(body.get_rotation());
                    }
                    break;
                }
            }
        }
    }

    // ========== 内部实现 ==========

    void PhysicsSystem::sync_rigid_bodies()
    {
        auto go_ids = m_scene->find_game_objects_with_component<RigidBodyComponent>();
        for (GameObject::ID go_id : go_ids)
        {
            GameObject& go = m_scene->get_game_object(go_id);
            RigidBodyComponent* comp = go.get_component<RigidBodyComponent>();
            if (!comp) continue;

            // 首次创建：刚体尚未在 PhysicsWorld 中
            if (!comp->m_rigid_body.is_valid())
            {
                // 从 TransformComponent 获取初始位姿
                TransformComponent* transform = go.get_component<TransformComponent>();
                if (transform)
                {
                    comp->m_info.initial_position = transform->get_position();
                    comp->m_info.initial_rotation = transform->get_orientation();
                }

                comp->m_rigid_body = m_physics_world.add_rigid_body(comp->m_info);
                comp->m_world       = &m_physics_world;
                comp->m_need_sync   = false;

                if (!comp->m_rigid_body.is_valid())
                {
                    ID_ERROR("PhysicsSystem: 创建刚体失败 (GO: '{}', ID={})", go.get_name(), go_id);
                }
            }
            // 需要同步：CreateInfo 有变更
            else if (comp->m_need_sync)
            {
                // 更新 m_world 引用（防御性）
                comp->m_world = &m_physics_world;

                RigidBody& body = m_physics_world.get_rigid_body(comp->m_rigid_body);

                // 同步可变更属性到已有刚体
                body.set_mass(comp->m_info.mass);
                body.set_type(comp->m_info.type);
                body.set_linear_damping(comp->m_info.linear_damping);
                body.set_angular_damping(comp->m_info.angular_damping);
                body.set_allow_sleep(comp->m_info.allow_sleep);

                comp->m_need_sync = false;
            }
            else
            {
                // 确保 m_world 引用有效
                comp->m_world = &m_physics_world;
            }
        }

        // 清理：移除已销毁的 GameObject 对应的刚体
        // 通过比较 Scene 中的 RigidBodyComponent 与 PhysicsWorld 中的刚体
        // （简化实现：Scene::destroy_game_object → RigidBodyComponent::on_detach → m_rigid_body 变 invalid
        //  然后在下一帧的 sync 中，PhysicsWorld 检测不到重新创建的，由组件析构保证清理）
        //
        // 注：当前设计中 GameObject 销毁时，RigidBodyComponent::on_detach 清空 m_rigid_body，
        //     但 PhysicsWorld 中仍残留。如需彻底清理，可在 on_detach 或此处遍历 PhysicsWorld 刚体。
    }

    void PhysicsSystem::push_transforms()
    {
        auto go_ids = m_scene->find_game_objects_with_component<RigidBodyComponent>();
        for (GameObject::ID go_id : go_ids)
        {
            GameObject& go = m_scene->get_game_object(go_id);
            RigidBodyComponent* comp = go.get_component<RigidBodyComponent>();
            if (!comp || !comp->m_rigid_body.is_valid()) continue;

            RigidBody& body = m_physics_world.get_rigid_body(comp->m_rigid_body);

            // Static：只同步一次（首次创建已设置），之后不需要
            // Kinematic：每帧推送 Transform → 物理引擎
            if (body.is_kinematic())
            {
                TransformComponent* transform = go.get_component<TransformComponent>();
                if (transform)
                {
                    body.set_transform(transform->get_position(), transform->get_orientation());
                }
            }
        }
    }

    void PhysicsSystem::pull_transforms()
    {
        auto go_ids = m_scene->find_game_objects_with_component<RigidBodyComponent>();
        for (GameObject::ID go_id : go_ids)
        {
            GameObject& go = m_scene->get_game_object(go_id);
            RigidBodyComponent* comp = go.get_component<RigidBodyComponent>();
            if (!comp || !comp->m_rigid_body.is_valid()) continue;

            RigidBody& body = m_physics_world.get_rigid_body(comp->m_rigid_body);

            // Dynamic：拉取 PhysicsWorld → TransformComponent
            if (body.is_dynamic())
            {
                TransformComponent* transform = go.get_component<TransformComponent>();
                if (transform)
                {
                    Vec3 phys_pos = body.get_position();

                    // 只有位置变了才写入，避免无谓触发 dirty 传播
                    if (phys_pos != transform->get_position())
                    {
                        transform->set_position(phys_pos);
                        transform->set_orientation(body.get_rotation());
                    }
                }
            }
        }
    }
} // namespace ID
