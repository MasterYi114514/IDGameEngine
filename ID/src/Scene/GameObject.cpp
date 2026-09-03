#include "Scene/GameObject.hpp"
#include "Scene/Scene.hpp"
#include "Log/Log.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDWindow.hpp"

namespace ID
{
    GameObject GameObject::default_game_object{nullptr, GameObject::INVALID_ID, "Default GameObject"};

    // 组件由 Scene 的 ComponentRegistry 持有，本类不再拥有/释放组件
    // （销毁路径：Scene::destroy_game_object / ~Scene 先 erase_all_of 触发 on_detach）
    GameObject::~GameObject() { }

    GameObject::GameObject(Scene* scene, ID id, const std::string& name)
        : m_scene(scene), m_id(id), m_name(name), m_is_active(false) { }

    ComponentRegistry& GameObject::get_registry()
    {
        return m_scene->get_component_registry();
    }

    const ComponentRegistry& GameObject::get_registry() const
    {
        return m_scene->get_component_registry();
    }

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

        // 按池顺序（TypeID 升序）遍历本 GO 的组件（池化后遍历顺序与旧链表顺序不同，
        // 现有组件无顺序依赖，见迁移计划坑 E）
        get_registry().for_each_component_of(m_id,
            [ts](Component::TypeID, Component& component)
            {
                if (component.is_active())
                {
                    component.on_update(ts);
                }
            });
    }

    void GameObject::on_event(Event& event)
    {
        if (!m_is_active) return;

        get_registry().for_each_component_of(m_id,
            [&event](Component::TypeID, Component& component)
            {
                if (event.is_handled()) return;      // 事件已被处理则不再传递
                if (component.is_active())
                {
                    component.on_event(event);
                }
            });
    }

    Json GameObject::serialize(ArenaID arena_id) const
    {
        Json result = Json::create_object(arena_id);
        result.insert("name", Json::create_string(m_name, arena_id));
        result.insert("is_active", Json(m_is_active));

        // 序列化 Components（按池顺序，即 TypeID 升序；旧存档按 type 字段反序列化，双向兼容）
        Json components_array = Json::create_array(arena_id);
        get_registry().for_each_component_of(m_id,
            [&](Component::TypeID, const Component& component)
            {
                components_array.push_back(component.serialize(arena_id));
            });
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

        // 组件由 Scene 的 ComponentRegistry 持有：本函数不重复清空旧组件，
        // 重复反序列化同一 GO 前应由调用方保证组件已清理（Scene::deserialize 全量重建场景）

        // 反序列化 Components：Creator 内部完成入池 + 反序列化（重复类型会被跳过）
        const Json& components_array = json["components"];
        if (components_array.is_array())
        {
            for (size_t i = 0; i < components_array.size(); ++i)
            {
                const Json& component_json = components_array[i];
                std::string type_name = component_json["type"].as_cstr();

                if (!ComponentFactory::create(type_name, *this, component_json))
                {
                    ID_ERROR("无法创建组件类型: {}（未注册或该 GO 已持有同类型组件）", type_name);
                }
            }
        }

        // children 的递归创建由 Scene::deserialize / deserialize_tree 负责
        // GameObject::deserialize 只反序列化自身字段，不做递归
    }
} // namespace ID