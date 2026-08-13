#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"
#include "Scene/GameObject.hpp"

namespace ID
{
    class Scene;
    class ImGuiLayer;
    class TransformComponent;
    class MeshRendererComponent;
    class LightComponent;
    class RigidBodyComponent;
    class AudioSourceComponent;
    class AudioListenerComponent;

    /*
    *   InspectorPanel — 属性检视器
    *
    *   读取 ImGuiLayer 的全局选中状态，展示并编辑选中 GameObject 的全部属性：
    *   - 基础信息：名称 / Active
    *   - Transform：Position / Rotation(Euler) / Scale
    *   - 各 Component 独立折叠块（MeshRenderer / Light / RigidBody / AudioSource / AudioListener）
    *   - Add Component 下拉菜单（调用 GameObject::add_component<T>()）
    */
    class ID_API InspectorPanel : public ImGuiPanel
    {
    public:
        explicit InspectorPanel(ImGuiLayer* imgui_layer);

        void on_imgui_render() override;

        // 每帧由 ImGuiLayer 注入（场景 + 当前选中 ID）
        void set_context(Scene* scene, GameObject::ID selected_id)
        {
            m_context = scene;
            m_selected_id = selected_id;
        }

    private:
        void render_game_object_header(GameObject& go);
        void render_transform_editor(TransformComponent& transform);
        void render_mesh_renderer_editor(MeshRendererComponent& mesh);
        void render_light_editor(LightComponent& light);
        void render_rigid_body_editor(RigidBodyComponent& rigid_body);
        void render_audio_source_editor(AudioSourceComponent& audio);
        void render_audio_listener_editor(AudioListenerComponent& listener);
        void render_add_component_menu(GameObject& go);

    private:
        ImGuiLayer*    m_imgui_layer = nullptr;    // 非拥有引用
        Scene*         m_context = nullptr;
        GameObject::ID m_selected_id = GameObject::INVALID_ID;
    };
} // namespace ID
