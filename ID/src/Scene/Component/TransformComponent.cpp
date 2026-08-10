#include "Scene/Component/Component.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Scene.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDJson.hpp"

#include "Log/Log.hpp"

namespace ID
{   
    ID_REGISTER_COMPONENT(TransformComponent, "TransformComponent");

    TransformComponent::TransformComponent() : m_pose(), m_scale(1.0f, 1.0f, 1.0f), m_dirty(true) { }

    void TransformComponent::translate(const Vec3& delta)
    {
        m_pose.position += delta;
        make_dirty();
    }

    void TransformComponent::rotate(const Quat& delta)
    {
        m_pose.orientation = delta * m_pose.orientation; // 局部坐标系下叠加旋转
        make_dirty();
    }

    void TransformComponent::scale(const Vec3& factor)
    {
        m_scale *= factor;
        make_dirty();
    }

    void TransformComponent::make_dirty() const
    {
        m_dirty = true;
        m_world_dirty = true;

        // 向下传播：所有子节点的世界矩阵也脏了
        GameObject* owner = get_owner();
        if (!owner || !owner->get_scene()) return;

        for(GameObject::ID child_id : owner->get_children())
        {
            GameObject& child = owner->get_scene()->get_game_object(child_id);
            TransformComponent* child_transform = child.get_component<TransformComponent>();
            if (child_transform)
            {
                child_transform->propagate_world_dirty();
            }
            else
            {
                ID_ERROR("TransformComponent::make_dirty: 子 GameObject 没有 TransformComponent");
            }
        }
    }

    void TransformComponent::propagate_world_dirty() const
    {
        m_world_dirty = true;

        GameObject* owner = get_owner();
        if (!owner || !owner->get_scene()) return;

        for (GameObject::ID child_id : owner->get_children())
        {
            GameObject& child = owner->get_scene()->get_game_object(child_id);
            TransformComponent* child_transform = child.get_component<TransformComponent>();
            if (child_transform)
            {
                child_transform->propagate_world_dirty();
            }
            else
            {
                ID_ERROR("TransformComponent::propagate_world_dirty: 子 GameObject 没有 TransformComponent");
            }
        }
    }

    Mat4 TransformComponent::get_model_matrix()
    {
        if(!is_dirty()) return m_model;

        m_model = m_pose.get_transform_matrix() * Math::get_scale(m_scale);
        clear_dirty();
        return m_model;
    }

    const Mat4& TransformComponent::get_model_matrix() const
    {
        if(!is_dirty()) return m_model;

        m_model = m_pose.get_transform_matrix() * Math::get_scale(m_scale);
        clear_dirty();
        return m_model;
    }

    const Mat4& TransformComponent::get_world_matrix() const
    {
        if (!m_world_dirty) return m_world_cache;

        const Mat4& local = get_model_matrix();

        GameObject* owner = get_owner();
        if (owner && owner->get_parent_id() != GameObject::INVALID_ID)
        {
            // ★ 必须在父 GameObject 上查找 TransformComponent，而不是在自身查找
            //   （自身查找会拿到自己的 Transform 导致 get_world_matrix 无限递归 → 栈溢出）
            GameObject& parent = owner->get_scene()->get_game_object(owner->get_parent_id());
            TransformComponent* parent_transform = parent.get_component<TransformComponent>();
            if (parent_transform)
            {
                m_world_cache = parent_transform->get_world_matrix() * local;
            }
            else
            {
                ID_ERROR("TransformComponent::get_world_matrix: 父 GameObject '{}' (ID={}) 没有 TransformComponent",
                    parent.get_name(), parent.get_id());
            }
        }
        else
        {
            m_world_cache = local;
        }

        m_world_dirty = false;
        return m_world_cache;
    }

    Json TransformComponent::serialize(ArenaID arena) const
    {
        Json obj = Json::create_object(arena);
        obj.insert("type", Json::create_string(get_component_type_name(), arena));
        obj.insert("m_pose", m_pose.serialize(arena));
        obj.insert("m_scale", JSON::create(m_scale, arena));
        return obj;
    }

    void TransformComponent::deserialize(const Json& json)
    {
        if (!json.is_object()) return;

        m_pose.deserialize(json["m_pose"]);
        m_scale = JSON::parse<Vec3>(json["m_scale"]);
        
        make_dirty();
    }
} // namespace ID