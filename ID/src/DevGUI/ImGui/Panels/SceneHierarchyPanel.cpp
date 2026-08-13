#include "DevGUI/ImGui/Panels/SceneHierarchyPanel.hpp"

#include "DevGUI/ImGui/ImGuiLayer.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"
#include "Scene/Component/LightComponent.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Log/Log.hpp"

#include "IDJson.hpp"

namespace ID
{
    SceneHierarchyPanel::SceneHierarchyPanel(ImGuiLayer* imgui_layer)
        : ImGuiPanel("Scene Hierarchy", true), m_imgui_layer(imgui_layer) { }

    void SceneHierarchyPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        if(!m_context)
        {
            ImGui::Text("无活跃场景");
            end_window();
            return;
        }

        render_scene_root();

        ImGui::Separator();
        render_create_buttons();

        end_window();
    }

    // =====================================================================
    //  场景根节点 + 递归层级树
    // =====================================================================

    void SceneHierarchyPanel::render_scene_root()
    {
        if(ImGui::TreeNodeEx("##scene_root", ImGuiTreeNodeFlags_DefaultOpen,
            "Scene: %s", m_context->get_name().c_str()))
        {
            // 直接 O(n) 遍历内部数组（无分配），跳过空槽位，只渲染根节点
            // 子节点由 render_node 递归处理
            const size_t capacity = m_context->get_game_object_capacity();
            for(size_t i = 0; i < capacity; ++i)
            {
                const GameObject::ID id = static_cast<GameObject::ID>(i);
                if(!m_context->is_game_object_valid(id)) continue;
                if(m_context->get_game_object(id).get_parent_id() == GameObject::INVALID_ID)
                {
                    render_node(id);
                }
            }
            ImGui::TreePop();
        }
    }

    void SceneHierarchyPanel::render_node(GameObject::ID id)
    {
        GameObject& go = m_context->get_game_object(id);
        const std::vector<GameObject::ID>& children = go.get_children();

        const bool is_selected = (m_imgui_layer->get_selected_object() == id);
        const bool has_children = !children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth;
        if(is_selected)   flags |= ImGuiTreeNodeFlags_Selected;
        if(!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // 稳定的节点 ID：GO ID（防止重名/增删导致树状态错乱）
        ImGui::PushID(static_cast<int>(id));

        // 激活状态标记：失活物体名称置灰
        if(!go.is_active())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        }
        const bool open = ImGui::TreeNodeEx(go.get_name().c_str(), flags);
        if(!go.is_active())
        {
            ImGui::PopStyleColor();
        }

        // 点击选中（排除点击展开箭头）
        if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_imgui_layer->set_selected_object(id);
        }

        // 右键菜单
        if(ImGui::BeginPopupContextItem())
        {
            render_context_menu(id);
            ImGui::EndPopup();
        }

        handle_drag_drop(id);

        // 递归子节点
        if(open && has_children)
        {
            for(GameObject::ID child : children)
            {
                render_node(child);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // =====================================================================
    //  拖拽：重设父子关系
    // =====================================================================

    void SceneHierarchyPanel::handle_drag_drop(GameObject::ID id)
    {
        // 作为拖拽源
        if(ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("GO_NODE", &id, sizeof(GameObject::ID));
            ImGui::Text("%s", m_context->get_game_object(id).get_name().c_str());
            ImGui::EndDragDropSource();
        }

        // 作为拖拽目标（成为被拖拽节点的父节点）
        if(ImGui::BeginDragDropTarget())
        {
            if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GO_NODE"))
            {
                const GameObject::ID dragged = *static_cast<const GameObject::ID*>(payload->Data);

                // 防环：不能把节点拖到自己的后代上
                if(dragged != id && !is_descendant_of(dragged, id))
                {
                    m_context->get_game_object(dragged).set_parent(id);
                }
                else
                {
                    ID_WARN("[Hierarchy] 拖拽被拒绝：不能将节点挂到自身或其子节点下");
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    bool SceneHierarchyPanel::is_descendant_of(GameObject::ID source, GameObject::ID target) const
    {
        // 沿 target 的父链向上找，若遇到 source 则说明 target 是 source 的后代
        GameObject::ID current = target;
        while(current != GameObject::INVALID_ID)
        {
            if(current == source) return true;
            current = m_context->get_game_object(current).get_parent_id();
        }
        return false;
    }

    // =====================================================================
    //  右键菜单：Delete / Duplicate / Create Child
    // =====================================================================

    void SceneHierarchyPanel::render_context_menu(GameObject::ID id)
    {
        if(ImGui::MenuItem("Create Child"))
        {
            const GameObject::ID child = m_context->create_game_object("New Child");
            m_context->get_game_object(child).set_parent(id);
            m_imgui_layer->set_selected_object(child);
        }
        if(ImGui::MenuItem("Duplicate"))
        {
            duplicate_object(id);
        }
        ImGui::Separator();
        if(ImGui::MenuItem("Delete"))
        {
            if(m_imgui_layer->get_selected_object() == id)
            {
                m_imgui_layer->set_selected_object(GameObject::INVALID_ID);
            }
            m_context->destroy_game_object(id);
        }
    }

    GameObject::ID SceneHierarchyPanel::duplicate_object(GameObject::ID id)
    {
        ArenaID arena = ArenaManager::create_arena();
        const Json json = m_context->get_game_object(id).serialize(arena);
        const GameObject::ID new_id = clone_tree(json, GameObject::INVALID_ID);
        ArenaManager::destroy_arena(arena);

        m_imgui_layer->set_selected_object(new_id);
        ID_INFO("[Hierarchy] 已复制 '{}' (id={})", m_context->get_game_object(new_id).get_name(), new_id);
        return new_id;
    }

    GameObject::ID SceneHierarchyPanel::clone_tree(const Json& json, GameObject::ID parent_id)
    {
        const GameObject::ID id = m_context->create_game_object(json["name"].as_cstr());
        GameObject& go = m_context->get_game_object(id);
        go.deserialize(json);
        go.set_parent(parent_id);

        // 递归复制子节点
        const Json& children = json["children"];
        if(children.is_array())
        {
            for(size_t i = 0; i < children.size(); ++i)
            {
                clone_tree(children[i], id);
            }
        }
        return id;
    }

    // =====================================================================
    //  底部快捷创建按钮
    // =====================================================================

    void SceneHierarchyPanel::render_create_buttons()
    {
        if(ImGui::Button("+ Empty"))
        {
            const GameObject::ID id = m_context->create_game_object("Empty Object");
            m_imgui_layer->set_selected_object(id);
        }

        ImGui::SameLine();
        if(ImGui::Button("+ Light"))
        {
            const GameObject::ID id = m_context->create_game_object("Light");
            m_context->get_game_object(id).add_component<LightComponent>().make_active();
            m_imgui_layer->set_selected_object(id);
        }

        ImGui::SameLine();
        if(ImGui::Button("+ Cube"))
        {
            const GameObject::ID id = m_context->create_game_object("Cube");
            m_context->get_game_object(id).add_component<MeshRendererComponent>(
                Model(MeshFactory::create_cube(1.0f), default_material_instance)).make_active();
            m_imgui_layer->set_selected_object(id);
        }

        if(ImGui::Button("+ Sphere"))
        {
            const GameObject::ID id = m_context->create_game_object("Sphere");
            m_context->get_game_object(id).add_component<MeshRendererComponent>(
                Model(MeshFactory::create_sphere(0.5f), default_material_instance)).make_active();
            m_imgui_layer->set_selected_object(id);
        }
    }
} // namespace ID
