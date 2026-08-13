#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"
#include "Scene/GameObject.hpp"

#include <functional>

namespace ID
{
    class Scene;
    class ImGuiLayer;
    class Json;

    /*
    *   SceneHierarchyPanel — 场景层级树
    *
    *   递归遍历 Scene 中所有 GameObject，按父子关系展示树形结构。
    *   - 点击节点 → ImGuiLayer::set_selected_object()（Hierarchy → Inspector 通信）
    *   - 右键菜单：Delete / Duplicate / Create Child
    *   - 拖拽节点到另一节点上：重设父子关系（防环）
    *   - 底部快捷创建按钮：Empty / Light / Cube / Sphere
    */
    class ID_API SceneHierarchyPanel : public ImGuiPanel
    {
    public:
        explicit SceneHierarchyPanel(ImGuiLayer* imgui_layer);

        void on_imgui_render() override;

        // 设置活跃 Scene（通常由 ImGuiLayer 每帧注入 SceneManager::get_current_scene()）
        void set_context(Scene* scene) { m_context = scene; }

    private:
        void render_scene_root();
        void render_node(GameObject::ID id);
        void render_create_buttons();
        void render_context_menu(GameObject::ID id);
        void handle_drag_drop(GameObject::ID id);

        // 复制一个 GameObject（含递归子节点）
        GameObject::ID duplicate_object(GameObject::ID id);
        GameObject::ID clone_tree(const Json& json, GameObject::ID parent_id);

        // 判断 target 是否为 source 的后代（拖拽防环）
        bool is_descendant_of(GameObject::ID source, GameObject::ID target) const;

    private:
        ImGuiLayer* m_imgui_layer = nullptr;    // 非拥有引用，用于读写全局选中状态
        Scene*      m_context = nullptr;
    };
} // namespace ID
