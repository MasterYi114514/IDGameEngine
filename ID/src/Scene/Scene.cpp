#include "Scene/Scene.hpp"
#include "Log/Log.hpp"

namespace
{
    using namespace ID;
    GameObject::ID deserialize_tree(const Json& json, GameObject::ID parent_id, ID::Scene* scene)
    {
        GameObject::ID id = scene->create_game_object(json["name"].as_cstr());
        GameObject& go = scene->get_game_object(id);
        go.deserialize(json);
        go.set_parent(parent_id);

        const Json& children = json["children"];
        if (children.is_array())
        {
            for (size_t i = 0; i < children.size(); ++i)
            {
                deserialize_tree(children[i], id, scene);
            }
        }

        return id;
    }
} // 匿名命名空间

namespace ID
{
    Scene::Scene(const std::string& name) 
        : m_name(name), m_is_running(false), m_game_objects(), m_freed_ids()
    {
        ID_INFO("[Scene] 构造开始: '{}'", name);
        m_physics_system.on_attach(this);
        ID_INFO("[Scene] 构造完成: '{}'", name);
    }

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

    bool Scene::is_game_object_valid(GameObject::ID id) const
    {
        return id < m_game_objects.size() && m_game_objects[id] != nullptr;
    }

    void Scene::on_update(Timestep ts)
    {
        if (!m_is_running) return;

        // 物理系统先于 GameObject 更新（物理驱动 → 组件响应）
        m_physics_system.on_update(ts);

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

    Json Scene::serialize(ArenaID arena) const
    {
        Json result = Json::create_object(arena);
        result.insert("name", Json::create_string(m_name, arena));

        Json roots = Json::create_array(arena);
        for(const auto& go : m_game_objects)
        {
            // 只序列化根节点，子节点由 GameObject::serialize 递归处理
            if(go && go->get_parent_id() == GameObject::INVALID_ID)
            {
                roots.push_back(go->serialize(arena));
            }
        }
        result.insert("game_objects", roots);

        return result;
    }

    void Scene::deserialize(const Json& json)
    {
        m_name = json["name"].as_cstr();
        m_game_objects.clear();
        m_freed_ids.clear();

        const Json& roots = json["game_objects"];

        if(roots.is_array())
        {
            for(size_t i = 0; i < roots.size(); ++i)
            {
                deserialize_tree(roots[i], GameObject::INVALID_ID, this);
            }
        }
    }
} // namespace ID