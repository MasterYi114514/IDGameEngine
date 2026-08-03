#include "Scene/Scene.hpp"
#include "Log/Log.hpp"

namespace ID
{
    Scene::Scene(const std::string& name) 
        : m_name(name), m_is_running(false), m_game_objects(), m_freed_ids() { }

    GameObject::ID Scene::create_game_object(const std::string& name)
    {
        GameObject::ID id;
        if (!m_freed_ids.empty())
        {
            // 复用空闲的槽位
            id = *m_freed_ids.begin();
            m_freed_ids.erase(m_freed_ids.begin());
            m_game_objects[id] = std::unique_ptr<GameObject>(new GameObject(this, id, name));
        }
        else
        {
            // 在末尾创建一个新的 GameObject
            id = static_cast<GameObject::ID>(m_game_objects.size());
            m_game_objects.push_back(std::unique_ptr<GameObject>(new GameObject(this, id, name)));
        }
        return id;
    }

    void Scene::destroy_game_object(GameObject::ID id)
    {
        if(id >= m_game_objects.size() || !m_game_objects[id])
        {
            ID_ERROR("尝试销毁无效的 GameObject ID: {}", id);
            return;
        }

        // 递归销毁子节点
        const auto children = m_game_objects[id]->get_children();  // 拷贝
        for (GameObject::ID child_id : children)
        {
            destroy_game_object(child_id);
        }

        // 从父节点的子节点列表中移除自己
        GameObject& game_object = *m_game_objects[id];
        if (game_object.get_parent_id() != GameObject::INVALID_ID)
        {
            // 获取父节点
            GameObject& parent = get_game_object(game_object.get_parent_id());
            auto& siblings = parent.get_children();
            siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
        }

        // 销毁 GameObject
        m_game_objects[id].reset();
        m_freed_ids.insert(id);
    }

    void Scene::on_update(Timestep ts)
    {
        if (!m_is_running) return;

        for (const auto& game_object_ptr : m_game_objects)
        {
            if (game_object_ptr && game_object_ptr->is_active())
            {
                game_object_ptr->on_update(ts);
            }
        }
    }

    void Scene::on_event(Event& event)
    {
        if (!m_is_running) return;
        
        for (const auto& game_object_ptr : m_game_objects)
        {
            if (game_object_ptr && game_object_ptr->is_active())
            {
                game_object_ptr->on_event(event);
            }
        }
    }
} // namespace ID