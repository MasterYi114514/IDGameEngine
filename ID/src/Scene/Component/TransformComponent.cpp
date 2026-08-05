#include "Scene/Component/Component.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Scene.hpp"

namespace ID
{
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

        for (GameObject::ID child_id : owner->get_children())
        {
            GameObject& child = owner->get_scene()->get_game_object(child_id);
            child.get_transform().propagate_world_dirty();
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
            child.get_transform().propagate_world_dirty();
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
            const GameObject& parent = owner->get_scene()->get_game_object(owner->get_parent_id());
            m_world_cache = parent.get_transform().get_world_matrix() * local;
        }
        else
        {
            m_world_cache = local;
        }

        m_world_dirty = false;
        return m_world_cache;
    }
} // namespace ID