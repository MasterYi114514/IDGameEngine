#include "Scene/GameObject.hpp"
#include "Scene/Scene.hpp"
#include "Log/Log.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDWindow.hpp"

namespace ID
{
    GameObject GameObject::default_game_object{nullptr, GameObject::INVALID_ID, "Default GameObject"};

    GameObject::~GameObject()
    {
        for (auto& component : m_components)
        {
            if (component)
            {
                component->on_detach();
            }
        }
        m_components.clear();
    }

    GameObject::GameObject(Scene* scene, ID id, const std::string& name)
        : m_scene(scene), m_id(id), m_name(name), m_is_active(true) { }

    void GameObject::set_parent(ID parent_id)
    {
        // 如果有父节点，则从父节点的子节点列表中移除自己
        if(m_parent_id != INVALID_ID)
        {
            GameObject& old_parent = m_scene->get_game_object(m_parent_id);
            auto& siblings = old_parent.get_children();

            siblings.erase(std::remove(siblings.begin(), siblings.end(), m_id), siblings.end());
        }
        m_parent_id = parent_id;

        // 注册到新父节点的子节点列表中
        if(m_parent_id != INVALID_ID)
        {
            GameObject& new_parent = m_scene->get_game_object(m_parent_id);
            new_parent.get_children().push_back(m_id);
        }
    }

    GameObject& GameObject::get_parent() const
    {
        if (m_parent_id == INVALID_ID)
        {
            ID_ERROR("尝试访问一个没有父节点的 GameObject 的父节点");
            return default_game_object;             // 返回默认的 GameObject，避免使用空引用
        }
        return m_scene->get_game_object(m_parent_id);
    }

    void GameObject::on_update(Timestep ts)
    {
        if (!m_is_active) return;

        // transform 没有 update 逻辑
        // m_transform.on_update(ts);

        for (auto& component : m_components)
        {
            if (component)
            {
                component->on_update(ts);
            }
        }
    }

    void GameObject::on_event(Event& event)
    {
        if (!m_is_active) return;

        // transform 没有 event 逻辑
        // m_transform.on_event(event);

        for (auto& component : m_components)
        {
            if(event.is_handled()) break;       // 如果事件已经被处理，则不再传递给其他组件
            if (component)
            {
                component->on_event(event);
            }
        }
    }

    Json GameObject::serialize(ArenaID arena_id) const
    {
        Json result = Json::create_object(arena_id);
        result.insert("name", Json::create_string(m_name, arena_id));
        result.insert("is_active", Json(m_is_active));

        // 序列化 Components
        Json components_array = Json::create_array(arena_id);
        for (const auto& component : m_components)
        {
            if (component)
            {
                components_array.push_back(component->serialize(arena_id));
            }
        }
        result.insert("components", components_array);

        // 序列化子节点
        Json children_array = Json::create_array(arena_id);
        for (const auto& child_id : m_children)
        {
            const auto& child = m_scene->get_game_object(child_id);
            children_array.push_back(child.serialize(arena_id));
        }

        result.insert("children", children_array);

        return result;
    }

    void GameObject::deserialize(const Json& json)
    {
        m_name = json["name"].as_cstr();
        m_is_active = json["is_active"].as_bool();

        // 清空旧的组件（防御性，确保反序列化可重复调用）
        m_components.clear();
        m_component_index.clear();

        // 反序列化 Components
        const Json& components_array = json["components"];
        if (components_array.is_array())
        {
            for (size_t i = 0; i < components_array.size(); ++i)
            {
                const Json& component_json = components_array[i];
                std::string type_name = component_json["type"].as_cstr();

                auto component = ComponentFactory::create(type_name);
                if (component)
                {
                    component->deserialize(component_json);
                    component->on_attach(this);

                    // 索引表：该类型尚无记录时写入，保证 get_component O(1)
                    Component::TypeID type_id = component->get_type_id();
                    if (m_component_index.find(type_id) == m_component_index.end())
                    {
                        m_component_index[type_id] = component.get();
                    }

                    m_components.push_back(std::move(component));
                }
                else
                {
                    ID_ERROR("无法创建组件类型: {}", type_name);
                }
            }
        }

        // children 的递归创建由 Scene::deserialize / deserialize_tree 负责
        // GameObject::deserialize 只反序列化自身字段，不做递归
    }
} // namespace ID