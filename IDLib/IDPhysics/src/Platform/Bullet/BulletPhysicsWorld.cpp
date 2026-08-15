/**
 *  @file BulletPhysicsWorld.cpp
 *  @brief PhysicsWorld 的 Bullet 实现（PIMPL）
 *
 *  本文件是 Bullet 头文件唯一出现的地方。
 */
#include "Physics/PhysicsWorld.hpp"
#include "Log/Log.hpp"

#include <btBulletDynamicsCommon.h>

namespace ID
{

struct PhysicsWorld::Impl
{
    btDefaultCollisionConfiguration                     m_collision_config;
    btCollisionDispatcher                               m_dispatcher{ &m_collision_config };
    btDbvtBroadphase                                    m_broadphase;
    btSequentialImpulseConstraintSolver                 m_solver;
    btDiscreteDynamicsWorld                             m_world{ &m_dispatcher, &m_broadphase, &m_solver, &m_collision_config };

    std::vector<RigidBody>                              m_rigid_body_pool;
    std::unordered_set<RigidBodyID::UnderlyingType>     m_freed_ids;
    size_t                                              m_active_count = 0;   // 实际存活刚体数（槽位复用后精确计数）
    PhysicsWorld::CollisionCallback                     m_collision_callback;

    RigidBodyID::UnderlyingType search_slot()
    {
        if (!m_freed_ids.empty()) { auto it = m_freed_ids.begin(); auto id = *it; m_freed_ids.erase(it); return id; }
        return static_cast<RigidBodyID::UnderlyingType>(m_rigid_body_pool.size());
    }

    btCollisionShape* create_bt_shape(const ColliderShape& shape)
    {
        switch (shape.type)
        {
            case ColliderShape::Type::Box:     return new btBoxShape(btVector3(shape.m_data.half_extents[0], shape.m_data.half_extents[1], shape.m_data.half_extents[2]));
            case ColliderShape::Type::Sphere:  return new btSphereShape(shape.m_data.radius);
            case ColliderShape::Type::Capsule: return new btCapsuleShape(shape.m_data.capsule.radius, shape.m_data.capsule.height);
            case ColliderShape::Type::Plane:   return new btStaticPlaneShape(btVector3(shape.m_data.plane.normal[0], shape.m_data.plane.normal[1], shape.m_data.plane.normal[2]), shape.m_data.plane.constant);
            default: IDPHYSICS_ERROR("Unknown collider shape type"); return new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
        }
    }
};

// ========== 构造/析构 ==========

PhysicsWorld::PhysicsWorld()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->m_world.setGravity(btVector3(0.0f, -9.81f, 0.0f));
    IDPHYSICS_INFO("PhysicsWorld created");
}

PhysicsWorld::~PhysicsWorld()
{
    size_t destroyed_count = 0;
    for (size_t i = 0; i < m_impl->m_rigid_body_pool.size(); ++i)
    {
        RigidBody& rb = m_impl->m_rigid_body_pool[i];
        if (btRigidBody* bt_rb = static_cast<btRigidBody*>(rb.get_native_handle()))
        {
            m_impl->m_world.removeRigidBody(bt_rb);
            void* user_ptr = bt_rb->getUserPointer();
            delete static_cast<RigidBodyID::UnderlyingType*>(user_ptr);   // 清理 user pointer
            delete bt_rb->getMotionState();
            delete bt_rb->getCollisionShape();
            delete bt_rb;
            ++destroyed_count;
        }
    }
    IDPHYSICS_INFO("物理世界已销毁：已从 Bullet 世界中移除并释放 {} 个刚体（碰撞形状/运动状态/用户指针），碰撞配置、调度器、宽相、约束求解器均已释放", destroyed_count);
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&& other) noexcept : m_impl(std::move(other.m_impl)) {}
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&& other) noexcept { if (this != &other) m_impl = std::move(other.m_impl); return *this; }

// ========== 世界设置 ==========

void PhysicsWorld::set_gravity(const Vec3& g) { m_impl->m_world.setGravity(btVector3(g[0], g[1], g[2])); }
Vec3 PhysicsWorld::get_gravity() const { btVector3 g = m_impl->m_world.getGravity(); return Vec3(g.x(), g.y(), g.z()); }

void PhysicsWorld::step_simulation(float dt, int max_sub_steps, float fixed_time_step)
{
    m_impl->m_world.stepSimulation(dt, max_sub_steps, fixed_time_step);

    // 遍历碰撞流形，触发碰撞回调
    if (!m_impl->m_collision_callback) return;

    int manifold_count = m_impl->m_dispatcher.getNumManifolds();
    for (int i = 0; i < manifold_count; ++i)
    {
        btPersistentManifold* mf = m_impl->m_dispatcher.getManifoldByIndexInternal(i);
        if (!mf || mf->getNumContacts() == 0) continue;

        const btCollisionObject* obj_a = mf->getBody0();
        const btCollisionObject* obj_b = mf->getBody1();

        void* ptr_a = obj_a ? obj_a->getUserPointer() : nullptr;
        void* ptr_b = obj_b ? obj_b->getUserPointer() : nullptr;
        if (!ptr_a || !ptr_b) continue;   // 非刚体（如 ground plane）无 user pointer，跳过

        RigidBodyID::UnderlyingType id_a = *static_cast<RigidBodyID::UnderlyingType*>(ptr_a);
        RigidBodyID::UnderlyingType id_b = *static_cast<RigidBodyID::UnderlyingType*>(ptr_b);
        if (m_impl->m_freed_ids.find(id_a) != m_impl->m_freed_ids.end()) continue;   // 已移除的刚体
        if (m_impl->m_freed_ids.find(id_b) != m_impl->m_freed_ids.end()) continue;

        CollisionInfo info;
        info.body_a = RigidBodyID{ id_a };
        info.body_b = RigidBodyID{ id_b };

        // 取第一个接触点的信息
        const btManifoldPoint& pt = mf->getContactPoint(0);
        const btVector3& pos = pt.getPositionWorldOnB();
        const btVector3& nrm = pt.m_normalWorldOnB;
        info.contact_point   = Vec3(pos.x(), pos.y(), pos.z());
        info.contact_normal  = Vec3(nrm.x(), nrm.y(), nrm.z());
        info.penetration_depth = -pt.getDistance();   // Bullet 中 getDistance() < 0 表示穿透

        m_impl->m_collision_callback(info);
    }
}

// ========== 刚体管理 ==========

RigidBodyID PhysicsWorld::add_rigid_body(const RigidBodyCreateInfo& info)
{
    btCollisionShape* bt_shape = m_impl->create_bt_shape(info.shape);

    btVector3 local_inertia(0.0f, 0.0f, 0.0f);
    if (info.type == RigidBodyType::Dynamic && info.mass > 0.0f)
        bt_shape->calculateLocalInertia(info.mass, local_inertia);

    btTransform bt_start;
    bt_start.setIdentity();
    bt_start.setOrigin(btVector3(info.initial_position[0], info.initial_position[1], info.initial_position[2]));
    bt_start.setRotation(btQuaternion(info.initial_rotation.x(), info.initial_rotation.y(), info.initial_rotation.z(), info.initial_rotation.w()));

    btDefaultMotionState* ms = new btDefaultMotionState(bt_start);

    btRigidBody::btRigidBodyConstructionInfo bt_info(info.mass, ms, bt_shape, local_inertia);
    bt_info.m_friction        = info.material.friction;
    bt_info.m_restitution     = info.material.restitution;
    bt_info.m_rollingFriction = info.material.rolling_friction;
    bt_info.m_linearDamping   = info.linear_damping;
    bt_info.m_angularDamping  = info.angular_damping;

    btRigidBody* bt_rb = new btRigidBody(bt_info);

    switch (info.type)
    {
        case RigidBodyType::Static:    bt_rb->setCollisionFlags(bt_rb->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT); break;
        case RigidBodyType::Kinematic: bt_rb->setCollisionFlags(bt_rb->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                                       bt_rb->setActivationState(DISABLE_DEACTIVATION); break;
        default: break;
    }
    if (!info.allow_sleep) bt_rb->setActivationState(DISABLE_DEACTIVATION);

    // 先分配槽位，再设置 user pointer（碰撞回调 / raycast 依赖它反查 RigidBodyID）
    RigidBodyID::UnderlyingType slot = m_impl->search_slot();
    bt_rb->setUserPointer(new RigidBodyID::UnderlyingType(slot));

    m_impl->m_world.addRigidBody(bt_rb);

    if (slot >= m_impl->m_rigid_body_pool.size())
        m_impl->m_rigid_body_pool.push_back(RigidBody(bt_rb));
    else
        m_impl->m_rigid_body_pool[slot] = RigidBody(bt_rb);

    ++m_impl->m_active_count;
    IDPHYSICS_DEBUG("RigidBody added: slot={}", slot);
    return RigidBodyID{ slot };
}

void PhysicsWorld::remove_rigid_body(RigidBodyID id)
{
    if (!is_rigid_body_valid(id)) return;
    RigidBody& rb = m_impl->m_rigid_body_pool[id.id];
    if (btRigidBody* bt_rb = static_cast<btRigidBody*>(rb.get_native_handle()))
    {
        m_impl->m_world.removeRigidBody(bt_rb);
        void* user_ptr = bt_rb->getUserPointer();
        delete static_cast<RigidBodyID::UnderlyingType*>(user_ptr);   // 清理 user pointer
        delete bt_rb->getMotionState();
        delete bt_rb->getCollisionShape();
        delete bt_rb;
    }
    rb.destroy();
    m_impl->m_freed_ids.insert(id.id);
    --m_impl->m_active_count;
    IDPHYSICS_DEBUG("RigidBody removed: slot={}", id.id);
}

RigidBody& PhysicsWorld::get_rigid_body(RigidBodyID id) { return m_impl->m_rigid_body_pool.at(id.id); }

bool PhysicsWorld::is_rigid_body_valid(RigidBodyID id) const
{
    return id.is_valid()
        && id.id < m_impl->m_rigid_body_pool.size()
        && m_impl->m_rigid_body_pool[id.id].is_valid()
        && m_impl->m_freed_ids.find(id.id) == m_impl->m_freed_ids.end();
}

size_t PhysicsWorld::get_rigid_body_count() const
{
    return m_impl->m_active_count;
}

// ========== 射线检测 ==========

RigidBodyID PhysicsWorld::raycast(const Vec3& from, const Vec3& to) const
{
    btVector3 bt_from(from[0], from[1], from[2]);
    btVector3 bt_to(to[0], to[1], to[2]);
    btCollisionWorld::ClosestRayResultCallback cb(bt_from, bt_to);
    m_impl->m_world.rayTest(bt_from, bt_to, cb);

    if (cb.hasHit())
    {
        // O(1)：通过 userPointer 直接反查 RigidBodyID，不再线性遍历
        const btCollisionObject* obj = cb.m_collisionObject;
        void* user_ptr = obj ? obj->getUserPointer() : nullptr;
        if (user_ptr)
        {
            RigidBodyID::UnderlyingType id = *static_cast<RigidBodyID::UnderlyingType*>(user_ptr);
            // 已移除的刚体（陈旧 userPointer）必须排除
            if (m_impl->m_freed_ids.find(id) == m_impl->m_freed_ids.end()
                && id < m_impl->m_rigid_body_pool.size()
                && m_impl->m_rigid_body_pool[id].is_valid())
            {
                return RigidBodyID{ id };
            }
        }
    }
    return RigidBodyID{ RigidBodyID::INVALID };
}

void PhysicsWorld::set_collision_callback(CollisionCallback cb) { m_impl->m_collision_callback = std::move(cb); }
void* PhysicsWorld::get_native_world() const { return &m_impl->m_world; }

} // namespace ID
