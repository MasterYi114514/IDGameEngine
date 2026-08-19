#pragma once

#include "IDPhysicsCore.hpp"
#include "IDMath.hpp"
#include "Physics/ColliderShape.hpp"
#include "Physics/PhysicsMaterial.hpp"

namespace ID
{
    class PhysicsWorld;

    /**
     *  刚体类型
     *  1. `Statc`：静态刚体，不受物理模拟影响，通常用于地形、墙壁等。
     *  2. `Dynamic`：动态刚体，受物理模拟影响，可移动、旋转。
     *  3. `Kinematic`：运动学刚体，不受物理模拟影响，但可以通过代码控制其位置和旋转，通常用于角色控制器等。
     */
    enum class RigidBodyType : uint8_t
    {
        Static    = 0,
        Dynamic   = 1,
        Kinematic = 2,
    };

    struct IDPHYSICS_API RigidBodyCreateInfo
    {
        RigidBodyType   type              = RigidBodyType::Dynamic;
        float           mass              = 1.0f;
        ColliderShape   shape;
        Vec3            initial_position  = Vec3();
        Quat            initial_rotation  = Quat::identity();
        PhysicsMaterial material;
        float           linear_damping    = 0.0f;
        float           angular_damping   = 0.0f;
        bool            allow_sleep       = true;
    };

    /**
     *  刚体类
     */
    class IDPHYSICS_API RigidBody
    {
        friend class PhysicsWorld;

    public:
        RigidBody() = default;
        ~RigidBody() { destroy(); }

        RigidBody(const RigidBody&) = delete;
        RigidBody& operator=(const RigidBody&) = delete;
        RigidBody(RigidBody&& other) noexcept;
        RigidBody& operator=(RigidBody&& other) noexcept;

        void destroy();

        // 质量与类型
        void  set_mass(float mass);
        float get_mass() const;

        void  set_type(RigidBodyType type);
        RigidBodyType get_type() const;

        bool is_static()    const { return get_type() == RigidBodyType::Static; }
        bool is_dynamic()   const { return get_type() == RigidBodyType::Dynamic; }
        bool is_kinematic() const { return get_type() == RigidBodyType::Kinematic; }

        // 阻尼
        void  set_linear_damping(float damping);
        void  set_angular_damping(float damping);
        float get_linear_damping() const;
        float get_angular_damping() const;

        // 材质
        void            set_material(const PhysicsMaterial& material);
        PhysicsMaterial get_material() const;

        // 力与冲量
        void apply_force(const Vec3& force);            // 施加一个力
        void apply_impulse(const Vec3& impulse);        // 施加一个冲量
        void apply_torque(const Vec3& torque);          // 施加一个力矩
        void clear_forces();                            // 清除所有施加的力和力矩

        // 速度
        void set_linear_velocity(const Vec3& velocity);
        Vec3 get_linear_velocity() const;
        void set_angular_velocity(const Vec3& velocity);
        Vec3 get_angular_velocity() const;

        // 变换（从物理模拟读取）
        Vec3 get_position() const;
        Quat get_rotation() const;
        Mat4 get_transform_matrix() const;

        // 手动变换（Kinematic 用）
        void set_transform(const Vec3& position, const Quat& rotation);

        // 睡眠
        void set_allow_sleep(bool allow);
        bool is_sleeping() const;
        void wake_up();

        // 内部
        void* get_native_handle() const { return m_native; }
        bool  is_valid() const { return m_native != nullptr; }

    private:
        explicit RigidBody(void* bt_rigid_body) : m_native(bt_rigid_body) {}
        void* m_native = nullptr;
    };
} // namespace ID
