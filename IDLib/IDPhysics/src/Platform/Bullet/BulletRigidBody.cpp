#include "Physics/RigidBody.hpp"
#include <btBulletDynamicsCommon.h>

#include <cstring>

namespace ID
{

// ========== 移动语义 ==========

RigidBody::RigidBody(RigidBody&& other) noexcept
    : m_native(other.m_native)
{
    other.m_native = nullptr;
}

RigidBody& RigidBody::operator=(RigidBody&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_native = other.m_native;
        other.m_native = nullptr;
    }
    return *this;
}

void RigidBody::destroy()
{
    m_native = nullptr;
}

// ========== 辅助 ==========

static btRigidBody* get_bt(RigidBody& rb)       { return static_cast<btRigidBody*>(rb.get_native_handle()); }
static const btRigidBody* get_bt(const RigidBody& rb) { return static_cast<const btRigidBody*>(rb.get_native_handle()); }

// ========== 质量与类型 ==========

void RigidBody::set_mass(float mass)
{
    btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return;
    btVector3 inertia(0.0f, 0.0f, 0.0f);
    if (mass > 0.0f)
        bt_rb->getCollisionShape()->calculateLocalInertia(mass, inertia);
    bt_rb->setMassProps(mass, inertia);
}

float RigidBody::get_mass() const
{
    const btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return 0.0f;
    float inv_mass = bt_rb->getInvMass();
    return (inv_mass > 0.0f) ? (1.0f / inv_mass) : 0.0f;
}

void RigidBody::set_type(RigidBodyType type)
{
    btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return;
    int flags = bt_rb->getCollisionFlags();
    flags &= ~(btCollisionObject::CF_STATIC_OBJECT | btCollisionObject::CF_KINEMATIC_OBJECT);
    switch (type)
    {
        case RigidBodyType::Static:    flags |= btCollisionObject::CF_STATIC_OBJECT; break;
        case RigidBodyType::Kinematic: flags |= btCollisionObject::CF_KINEMATIC_OBJECT; bt_rb->setActivationState(DISABLE_DEACTIVATION); break;
        case RigidBodyType::Dynamic:   bt_rb->setActivationState(ACTIVE_TAG); break;
    }
    bt_rb->setCollisionFlags(flags);
}

RigidBodyType RigidBody::get_type() const
{
    const btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return RigidBodyType::Dynamic;
    int flags = bt_rb->getCollisionFlags();
    if (flags & btCollisionObject::CF_STATIC_OBJECT)    return RigidBodyType::Static;
    if (flags & btCollisionObject::CF_KINEMATIC_OBJECT) return RigidBodyType::Kinematic;
    return RigidBodyType::Dynamic;
}

// ========== 阻尼 ==========

void RigidBody::set_linear_damping(float d)  { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->setDamping(d, bt_rb->getAngularDamping()); }
void RigidBody::set_angular_damping(float d) { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->setDamping(bt_rb->getLinearDamping(), d); }
float RigidBody::get_linear_damping() const  { const btRigidBody* bt_rb = get_bt(*this); return bt_rb ? bt_rb->getLinearDamping() : 0.0f; }
float RigidBody::get_angular_damping() const { const btRigidBody* bt_rb = get_bt(*this); return bt_rb ? bt_rb->getAngularDamping() : 0.0f; }

// ========== 材质 ==========

void RigidBody::set_material(const PhysicsMaterial& material)
{
    btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return;
    bt_rb->setFriction(material.friction);
    bt_rb->setRestitution(material.restitution);
    bt_rb->setRollingFriction(material.rolling_friction);
}

PhysicsMaterial RigidBody::get_material() const
{
    const btRigidBody* bt_rb = get_bt(*this);
    PhysicsMaterial material;   // 无有效刚体时返回默认材质
    if (!bt_rb) return material;
    material.friction         = bt_rb->getFriction();
    material.restitution      = bt_rb->getRestitution();
    material.rolling_friction = bt_rb->getRollingFriction();
    return material;
}

// ========== 力与冲量 ==========

void RigidBody::apply_force(const Vec3& force)   { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->applyCentralForce(btVector3(force[0], force[1], force[2])); }
void RigidBody::apply_impulse(const Vec3& impulse) { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->applyCentralImpulse(btVector3(impulse[0], impulse[1], impulse[2])); }
void RigidBody::apply_torque(const Vec3& torque)  { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->applyTorque(btVector3(torque[0], torque[1], torque[2])); }
void RigidBody::clear_forces()                    { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->clearForces(); }

// ========== 速度 ==========

void RigidBody::set_linear_velocity(const Vec3& v)  { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->setLinearVelocity(btVector3(v[0], v[1], v[2])); }
Vec3 RigidBody::get_linear_velocity() const         { const btRigidBody* bt_rb = get_bt(*this); if (!bt_rb) return Vec3(); const btVector3& v = bt_rb->getLinearVelocity(); return Vec3(v.x(), v.y(), v.z()); }
void RigidBody::set_angular_velocity(const Vec3& v) { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->setAngularVelocity(btVector3(v[0], v[1], v[2])); }
Vec3 RigidBody::get_angular_velocity() const        { const btRigidBody* bt_rb = get_bt(*this); if (!bt_rb) return Vec3(); const btVector3& v = bt_rb->getAngularVelocity(); return Vec3(v.x(), v.y(), v.z()); }

// ========== 变换 ==========

Vec3 RigidBody::get_position() const
{
    const btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return Vec3();
    const btVector3& o = bt_rb->getWorldTransform().getOrigin();
    return Vec3(o.x(), o.y(), o.z());
}

Quat RigidBody::get_rotation() const
{
    const btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return Quat::identity();
    const btQuaternion& q = bt_rb->getWorldTransform().getRotation();
    return Quat(q.w(), q.x(), q.y(), q.z());
}

Mat4 RigidBody::get_transform_matrix() const
{
    const btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return Math::get_identity_mat4();
    btScalar m[16];
    bt_rb->getWorldTransform().getOpenGLMatrix(m);
    // Bullet 输出 OpenGL 列优先数组，IDMath Mat4 同为列优先存储，可直接批量拷贝
    Mat4 result;
    std::memcpy(result.get_data(), m, sizeof(m));
    return result;
}

void RigidBody::set_transform(const Vec3& position, const Quat& rotation)
{
    btRigidBody* bt_rb = get_bt(*this);
    if (!bt_rb) return;
    btTransform tr;
    tr.setIdentity();
    tr.setOrigin(btVector3(position[0], position[1], position[2]));
    tr.setRotation(btQuaternion(rotation.x(), rotation.y(), rotation.z(), rotation.w()));
    bt_rb->setWorldTransform(tr);
    if (bt_rb->isKinematicObject())
    {
        bt_rb->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
        bt_rb->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
    }
}

// ========== 睡眠 ==========

void RigidBody::set_allow_sleep(bool allow) { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->setActivationState(allow ? ACTIVE_TAG : DISABLE_DEACTIVATION); }
bool RigidBody::is_sleeping() const         { const btRigidBody* bt_rb = get_bt(*this); return bt_rb ? !bt_rb->isActive() : false; }
void RigidBody::wake_up()                   { if (btRigidBody* bt_rb = get_bt(*this)) bt_rb->activate(true); }

} // namespace ID
