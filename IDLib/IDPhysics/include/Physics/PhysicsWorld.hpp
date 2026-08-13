#pragma once

#include "IDPhysicsCore.hpp"
#include "IDMath.hpp"
#include "Physics/RigidBody.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace ID
{
    struct IDPHYSICS_API RigidBodyID
    {
        using UnderlyingType = uint32_t;
        static constexpr UnderlyingType INVALID = static_cast<UnderlyingType>(-1);

        UnderlyingType id = INVALID;

        bool is_valid() const { return id != INVALID; }
        bool operator==(const RigidBodyID& other) const { return id == other.id; }
        bool operator!=(const RigidBodyID& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const RigidBodyID& rid) const
            {
                return std::hash<UnderlyingType>()(rid.id);
            }
        };
    };

    struct IDPHYSICS_API CollisionInfo
    {
        RigidBodyID body_a;
        RigidBodyID body_b;
        Vec3        contact_point;
        Vec3        contact_normal;
        float       penetration_depth = 0.0f;
    };

    class IDPHYSICS_API PhysicsWorld
    {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&& other) noexcept;
        PhysicsWorld& operator=(PhysicsWorld&& other) noexcept;

        // 世界设置
        void set_gravity(const Vec3& gravity);
        Vec3 get_gravity() const;

        /**
         *  运行物理模拟
         *  @brief 运行物理模拟，更新刚体的状态，每过 fixed_time_step 会进行一次物理模拟
         *  @param dt               时间步长（秒）
         *  @param max_sub_steps    最大子步数，超过该值的子步将被丢弃，默认为 4
         *  @param fixed_time_step  固定时间步长（秒），默认为 1/60
         */
        void step_simulation(float dt,
                             int   max_sub_steps   = 4,
                             float fixed_time_step = 1.0f / 60.0f);

        // 刚体管理
        RigidBodyID add_rigid_body(const RigidBodyCreateInfo& create_info);
        void        remove_rigid_body(RigidBodyID id);
        RigidBody&  get_rigid_body(RigidBodyID id);
        bool        is_rigid_body_valid(RigidBodyID id) const;
        size_t      get_rigid_body_count() const;

        // 射线检测
        RigidBodyID raycast(const Vec3& from, const Vec3& to) const;

        // 碰撞回调
        using CollisionCallback = std::function<void(const CollisionInfo& info)>;
        void set_collision_callback(CollisionCallback callback);

        // 内部
        void* get_native_world() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace ID
