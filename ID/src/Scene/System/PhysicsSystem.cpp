#include "Scene/System/PhysicsSystem.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/GameObject.hpp"

#include "Log/Log.hpp"

namespace ID
{
    namespace
    {
        /*
        *   判断 TransformComponent 与刚体的位姿是否出现偏差（逐分量精确比较）。
        *
        *   pull_transforms 会把物理世界的位姿原样写入 TransformComponent，因此正常
        *   运行中二者完全一致；出现任何偏差都说明 Transform 被外部代码（如 DevUI 的
        *   Inspector 面板）修改过，需要把新位姿传送到物理世界。
        */
        bool pose_diverged(const TransformComponent& transform, const RigidBody& body)
        {
            if (transform.get_position() != body.get_position()) return true;

            const Quat t_rot = transform.get_orientation();
            const Quat b_rot = body.get_rotation();
            return t_rot.x() != b_rot.x()
                || t_rot.y() != b_rot.y()
                || t_rot.z() != b_rot.z()
                || t_rot.w() != b_rot.w();
        }
    } // namespace

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

        // 阶段 2：Kinematic 每帧由 Transform 驱动；Static / Dynamic 在 Transform 被外部修改时传送位姿
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

        // 从 RigidBodyComponent 池反查持有该刚体的组件（下标遍历：owners 与 components 同下标）
        auto& pool = m_scene->get_component_registry().pool<RigidBodyComponent>();
        for (size_t i = 0; i < pool.size(); ++i)
        {
            RigidBodyComponent& comp = pool.components()[i];
            if (comp.m_rigid_body != rigid_body) continue;

            GameObject::ID go_id = pool.owners()[i];
            if (!m_scene->is_game_object_valid(go_id)) break;

            GameObject& go = m_scene->get_game_object(go_id);

            // 未激活的 GameObject / 组件不参与物理同步
            if (!go.is_active() || !comp.is_active()) break;

            TransformComponent* transform = go.get_component<TransformComponent>();
            if (transform && transform->is_active())
            {
                // Static / Kinematic：推送 Transform → 物理引擎；Dynamic：拉取 物理引擎 → Transform
                if (body.is_static() || body.is_kinematic())
                {
                    body.set_transform(transform->get_position(), transform->get_orientation());
                }
                else
                {
                    transform->set_position(body.get_position());
                    transform->set_orientation(body.get_rotation());
                }
            }
            break;      // 找到即退出（每刚体至多对应一个组件）
        }
    }

    // ========== 内部实现 ==========

    void PhysicsSystem::sync_rigid_bodies()
    {
        // 按池遍历（下标循环：本函数只修改 PhysicsWorld 与组件字段，不做池结构性修改）
        auto& pool = m_scene->get_component_registry().pool<RigidBodyComponent>();
        for (size_t i = 0; i < pool.size(); ++i)
        {
            GameObject::ID go_id = pool.owners()[i];
            if (!m_scene->is_game_object_valid(go_id)) continue;    // 防御：正常不触发（GO 销毁时组件已出池）

            GameObject& go = m_scene->get_game_object(go_id);
            RigidBodyComponent* comp = &pool.components()[i];

            // GameObject 或组件未激活：不参与物理模拟，移除已存在的刚体（若刚被停用）
            if (!go.is_active() || !comp->is_active())
            {
                if (comp->m_rigid_body.is_valid())
                {
                    m_physics_world.remove_rigid_body(comp->m_rigid_body);
                    comp->m_rigid_body = RigidBodyID{ RigidBodyID::INVALID };
                    comp->m_world       = nullptr;
                    comp->m_need_sync   = true;   // 重新激活后需要重建刚体
                }
                continue;
            }

            // 首次创建：刚体尚未在 PhysicsWorld 中
            if (!comp->m_rigid_body.is_valid())
            {
                // 从 TransformComponent 获取初始位姿
                TransformComponent* transform = go.get_component<TransformComponent>();
                if (transform && transform->is_active())
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
                body.set_material(comp->m_info.material);
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
        // 按池遍历（下标循环：本函数只读组件池，不做结构性修改）
        auto& pool = m_scene->get_component_registry().pool<RigidBodyComponent>();
        for (size_t i = 0; i < pool.size(); ++i)
        {
            GameObject::ID go_id = pool.owners()[i];
            if (!m_scene->is_game_object_valid(go_id)) continue;

            GameObject& go = m_scene->get_game_object(go_id);
            RigidBodyComponent* comp = &pool.components()[i];
            if (!comp->m_rigid_body.is_valid()) continue;
            if (!go.is_active() || !comp->is_active()) continue;      // 未激活：不推送

            TransformComponent* transform = go.get_component<TransformComponent>();
            if (!transform || !transform->is_active()) continue;

            RigidBody& body = m_physics_world.get_rigid_body(comp->m_rigid_body);

            // Kinematic：每帧由 Transform 驱动位姿
            // Static / Dynamic：仅当 Transform 被外部修改（与刚体位姿出现偏差）时才传送
            if (!body.is_kinematic() && !pose_diverged(*transform, body)) continue;

            body.set_transform(transform->get_position(), transform->get_orientation());

            // Dynamic 刚体传送后清零速度并唤醒，让它从编辑后的位姿重新开始模拟
            // （否则残留的线速度 / 角速度会让刚体立刻离开被编辑到的位置）
            if (body.is_dynamic())
            {
                // 注意：Vec3 默认构造不保证零初始化，必须显式写入零向量
                body.set_linear_velocity(Vec3(0.0f, 0.0f, 0.0f));
                body.set_angular_velocity(Vec3(0.0f, 0.0f, 0.0f));
                body.wake_up();
            }
        }
    }

    void PhysicsSystem::pull_transforms()
    {
        // 按池遍历（下标循环：本函数只读组件池，不做结构性修改）
        auto& pool = m_scene->get_component_registry().pool<RigidBodyComponent>();
        for (size_t i = 0; i < pool.size(); ++i)
        {
            GameObject::ID go_id = pool.owners()[i];
            if (!m_scene->is_game_object_valid(go_id)) continue;

            GameObject& go = m_scene->get_game_object(go_id);
            RigidBodyComponent* comp = &pool.components()[i];
            if (!comp->m_rigid_body.is_valid()) continue;
            if (!go.is_active() || !comp->is_active()) continue;      // 未激活：不拉取

            RigidBody& body = m_physics_world.get_rigid_body(comp->m_rigid_body);

            // Dynamic：拉取 PhysicsWorld → TransformComponent
            if (body.is_dynamic())
            {
                TransformComponent* transform = go.get_component<TransformComponent>();
                if (transform && transform->is_active())
                {
                    // 只有位姿真正变化才写入，避免每帧无谓触发 dirty 传播
                    if (pose_diverged(*transform, body))
                    {
                        transform->set_position(body.get_position());
                        transform->set_orientation(body.get_rotation());
                    }
                }
            }
        }
    }
} // namespace ID
