#include "Scene/Component/RigidBodyComponent.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDJson.hpp"
#include "IDMath.hpp"
#include "Log/Log.hpp"

namespace ID
{
    ID_REGISTER_COMPONENT(RigidBodyComponent, "RigidBodyComponent");

    // ========== Component 接口 ==========

    void RigidBodyComponent::on_attach(GameObject* owner)
    {
        Component::on_attach(owner);
    }

    void RigidBodyComponent::on_detach()
    {
        // 从 PhysicsWorld 中移除刚体
        if (m_world && m_rigid_body.is_valid())
        {
            m_world->remove_rigid_body(m_rigid_body);
        }

        m_rigid_body = RigidBodyID{ RigidBodyID::INVALID };
        m_world      = nullptr;
        m_need_sync  = true;
    }

    void RigidBodyComponent::on_update(Timestep /*ts*/)
    {
        // 物理驱动由 PhysicsSystem::on_update 统一处理
        // RigidBodyComponent 自身不做每帧更新
    }

    // ========== 物理属性 ==========

    void RigidBodyComponent::set_mass(float mass)
    {
        m_info.mass = mass;
        m_need_sync = true;

        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).set_mass(mass);
        }
    }

    float RigidBodyComponent::get_mass() const
    {
        // 优先从实际刚体读取（物理模拟中可能在后期被动态修改）
        if (m_world && m_rigid_body.is_valid())
        {
            return m_world->get_rigid_body(m_rigid_body).get_mass();
        }
        return m_info.mass;
    }

    void RigidBodyComponent::set_type(RigidBodyType type)
    {
        m_info.type = type;
        m_need_sync = true;
    }

    RigidBodyType RigidBodyComponent::get_type() const
    {
        if (m_world && m_rigid_body.is_valid())
        {
            return m_world->get_rigid_body(m_rigid_body).get_type();
        }
        return m_info.type;
    }

    bool RigidBodyComponent::is_static() const
    {
        return get_type() == RigidBodyType::Static;
    }

    bool RigidBodyComponent::is_dynamic() const
    {
        return get_type() == RigidBodyType::Dynamic;
    }

    bool RigidBodyComponent::is_kinematic() const
    {
        return get_type() == RigidBodyType::Kinematic;
    }

    // ========== Trigger ==========

    void RigidBodyComponent::set_trigger(bool trigger)
    {
        // Trigger 是引擎侧概念：Static + mass=0 + 不参与碰撞响应
        if (trigger)
        {
            m_info.type = RigidBodyType::Static;
            m_info.mass = 0.0f;
        }
        m_need_sync = true;
    }

    bool RigidBodyComponent::is_trigger() const
    {
        return m_info.type == RigidBodyType::Static && m_info.mass == 0.0f;
    }

    // ========== 阻尼 ==========

    void RigidBodyComponent::set_liner_damping(float damping)
    {
        m_info.linear_damping = damping;
        m_need_sync = true;

        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).set_linear_damping(damping);
        }
    }

    float RigidBodyComponent::get_linear_damping() const
    {
        if (m_world && m_rigid_body.is_valid())
        {
            return m_world->get_rigid_body(m_rigid_body).get_linear_damping();
        }
        return m_info.linear_damping;
    }

    void RigidBodyComponent::set_angular_damping(float damping)
    {
        m_info.angular_damping = damping;
        m_need_sync = true;

        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).set_angular_damping(damping);
        }
    }

    float RigidBodyComponent::get_angular_damping() const
    {
        if (m_world && m_rigid_body.is_valid())
        {
            return m_world->get_rigid_body(m_rigid_body).get_angular_damping();
        }
        return m_info.angular_damping;
    }

    // ========== 碰撞体形状 ==========

    void RigidBodyComponent::set_collider_shape(const ColliderShape& shape)
    {
        m_info.shape = shape;
        m_need_sync = true;
    }

    const ColliderShape& RigidBodyComponent::get_collider_shape() const
    {
        return m_info.shape;
    }

    // ========== 物理材质 ==========

    void RigidBodyComponent::set_material(const PhysicsMaterial& material)
    {
        m_info.material = material;
        m_need_sync = true;
    }

    const PhysicsMaterial& RigidBodyComponent::get_material() const
    {
        return m_info.material;
    }

    // ========== 力与速度 ==========

    void RigidBodyComponent::apply_force(const Vec3& force)
    {
        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).apply_force(force);
        }
        else
        {
            ID_WARN("RigidBodyComponent::apply_force：刚体尚未在 PhysicsWorld 中创建，操作被忽略");
        }
    }

    void RigidBodyComponent::apply_impulse(const Vec3& impulse)
    {
        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).apply_impulse(impulse);
        }
        else
        {
            ID_WARN("RigidBodyComponent::apply_impulse：刚体尚未在 PhysicsWorld 中创建，操作被忽略");
        }
    }

    void RigidBodyComponent::apply_torque(const Vec3& torque)
    {
        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).apply_torque(torque);
        }
        else
        {
            ID_WARN("RigidBodyComponent::apply_torque：刚体尚未在 PhysicsWorld 中创建，操作被忽略");
        }
    }

    void RigidBodyComponent::set_linear_velocity(const Vec3& velocity)
    {
        if (m_world && m_rigid_body.is_valid())
        {
            m_world->get_rigid_body(m_rigid_body).set_linear_velocity(velocity);
        }
        else
        {
            ID_WARN("RigidBodyComponent::set_linear_velocity：刚体尚未在 PhysicsWorld 中创建，操作被忽略");
        }
    }

    Vec3 RigidBodyComponent::get_linear_velocity() const
    {
        if (m_world && m_rigid_body.is_valid())
        {
            return m_world->get_rigid_body(m_rigid_body).get_linear_velocity();
        }
        return Vec3();
    }

    // ========== 序列化 ==========

    Json RigidBodyComponent::serialize(ArenaID arena) const
    {
        Json obj = Json::create_object(arena);
        obj.insert("type", Json::create_string(get_component_type_name(), arena));
        obj.insert("is_active", Json(m_is_active));

        // RigidBodyType → int
        obj.insert("rigid_body_type", Json(static_cast<int32_t>(m_info.type)));

        obj.insert("mass", Json(static_cast<double>(m_info.mass)));
        obj.insert("linear_damping", Json(static_cast<double>(m_info.linear_damping)));
        obj.insert("angular_damping", Json(static_cast<double>(m_info.angular_damping)));
        obj.insert("allow_sleep", Json(m_info.allow_sleep));

        // ColliderShape
        {
            Json shape_obj = Json::create_object(arena);
            shape_obj.insert("type", Json(static_cast<int32_t>(m_info.shape.type)));

            switch (m_info.shape.type)
            {
                case ColliderShape::Type::Box:
                    shape_obj.insert("half_extents", JSON::create(m_info.shape.m_data.half_extents, arena));
                    break;
                case ColliderShape::Type::Sphere:
                    shape_obj.insert("radius", Json(static_cast<double>(m_info.shape.m_data.radius)));
                    break;
                case ColliderShape::Type::Capsule:
                    shape_obj.insert("radius", Json(static_cast<double>(m_info.shape.m_data.capsule.radius)));
                    shape_obj.insert("height", Json(static_cast<double>(m_info.shape.m_data.capsule.height)));
                    break;
            }
            obj.insert("collider_shape", shape_obj);
        }

        // PhysicsMaterial
        {
            Json mat_obj = Json::create_object(arena);
            mat_obj.insert("friction", Json(static_cast<double>(m_info.material.friction)));
            mat_obj.insert("restitution", Json(static_cast<double>(m_info.material.restitution)));
            mat_obj.insert("rolling_friction", Json(static_cast<double>(m_info.material.rolling_friction)));
            obj.insert("material", mat_obj);
        }

        return obj;
    }

    void RigidBodyComponent::deserialize(const Json& json)
    {
        if (!json.is_object()) return;

        // RigidBodyType
        if (json.contains("rigid_body_type"))
            m_info.type = static_cast<RigidBodyType>(json["rigid_body_type"].as_int());

        if (json.contains("mass"))
            m_info.mass = static_cast<float>(json["mass"].as_float());

        if (json.contains("linear_damping"))
            m_info.linear_damping = static_cast<float>(json["linear_damping"].as_float());

        if (json.contains("angular_damping"))
            m_info.angular_damping = static_cast<float>(json["angular_damping"].as_float());

        if (json.contains("allow_sleep"))
            m_info.allow_sleep = json["allow_sleep"].as_bool();

        // ColliderShape
        if (json.contains("collider_shape"))
        {
            const Json& shape_json = json["collider_shape"];
            if (shape_json.is_object() && shape_json.contains("type"))
            {
                ColliderShape::Type shape_type = static_cast<ColliderShape::Type>(shape_json["type"].as_int());
                m_info.shape.type = shape_type;

                switch (shape_type)
                {
                    case ColliderShape::Type::Box:
                        if (shape_json.contains("half_extents"))
                            m_info.shape.m_data.half_extents = JSON::parse<Vec3>(shape_json["half_extents"]);
                        break;
                    case ColliderShape::Type::Sphere:
                        if (shape_json.contains("radius"))
                            m_info.shape.m_data.radius = static_cast<float>(shape_json["radius"].as_float());
                        break;
                    case ColliderShape::Type::Capsule:
                    {
                        if (shape_json.contains("radius"))
                            m_info.shape.m_data.capsule.radius = static_cast<float>(shape_json["radius"].as_float());
                        if (shape_json.contains("height"))
                            m_info.shape.m_data.capsule.height = static_cast<float>(shape_json["height"].as_float());
                        break;
                    }
                }
            }
        }

        // PhysicsMaterial
        if (json.contains("material"))
        {
            const Json& mat_json = json["material"];
            if (mat_json.is_object())
            {
                if (mat_json.contains("friction"))
                    m_info.material.friction = static_cast<float>(mat_json["friction"].as_float());
                if (mat_json.contains("restitution"))
                    m_info.material.restitution = static_cast<float>(mat_json["restitution"].as_float());
                if (mat_json.contains("rolling_friction"))
                    m_info.material.rolling_friction = static_cast<float>(mat_json["rolling_friction"].as_float());
            }
        }

        m_need_sync = true;

        // 恢复激活状态（刚体由 PhysicsSystem 在下一帧同步时创建，此处直接激活）
        if (json.contains("is_active") && json["is_active"].as_bool())
        {
            make_active();
        }
    }
} // namespace ID
