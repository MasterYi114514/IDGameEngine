#include "ID.hpp"
#include "Log/Log.hpp"
#include "Camera/CameraLayer.hpp"
#include "Scene/SceneLayer.hpp"
#include "Render/RenderLayer.hpp"

// ★ DevGUI：ImGui 调试面板系统
#include "DevGUI/ImGui/ImGuiLayer.hpp"
#include "DevGUI/ImGui/Panels/MenuBarPanel.hpp"
#include "DevGUI/ImGui/Panels/SceneHierarchyPanel.hpp"
#include "DevGUI/ImGui/Panels/InspectorPanel.hpp"
#include "DevGUI/ImGui/Panels/ConsolePanel.hpp"
#include "DevGUI/ImGui/Panels/StatsPanel.hpp"
#include "DevGUI/ImGui/Panels/SceneSettingsPanel.hpp"
#include "DevGUI/ImGui/Panels/CameraPanel.hpp"
#include "DevGUI/ImGui/Panels/RendererSettingsPanel.hpp"
#include "DevGUI/ImGui/Panels/PhysicsSettingsPanel.hpp"
#include "DevGUI/ImGui/Panels/AudioSettingsPanel.hpp"
#include "DevGUI/ImGui/Panels/AssetPanel.hpp"
#include "DevGUI/ImGui/Panels/ViewportPanel.hpp"

#include <functional>

class Sandbox : public ID::Application
{
public:
    Sandbox() : Application("Sandbox", 1280, 720)
    {
        ID_INFO("[Sandbox] 构造开始（Application 已构造完成）");

        m_camera_layer = new CameraLayer();
        ID_INFO("[Sandbox] CameraLayer 创建完成");
        push_layer(m_camera_layer);
        ID_INFO("[Sandbox] CameraLayer 入栈完成");

        m_scene_layer = new SceneLayer();
        ID_INFO("[Sandbox] SceneLayer 创建完成");
        push_layer(m_scene_layer);
        ID_INFO("[Sandbox] SceneLayer 入栈完成");

        m_render_layer = new ::RenderLayer(m_camera_layer);
        ID_INFO("[Sandbox] RenderLayer 创建完成");
        push_overlay(m_render_layer);

        // ---- 日志 sink：引擎日志 → ConsolePanel（注册回调，最小侵入）----
        Log::set_sink([](Log::Level level, const std::string& message)
        {
            ConsolePanel::add_log(message, static_cast<int>(level));
        });

        // ---- ★ DevGUI（overlay：最后渲染，最优先处理事件）----
        auto* imgui_layer = new ImGuiLayer("DevGUI");
        imgui_layer->add_panel<MenuBarPanel>();
        imgui_layer->add_panel<SceneHierarchyPanel>(imgui_layer);
        imgui_layer->add_panel<InspectorPanel>(imgui_layer);
        imgui_layer->add_panel<ConsolePanel>();
        imgui_layer->add_panel<StatsPanel>();
        imgui_layer->add_panel<SceneSettingsPanel>();

        CameraPanel& camera_panel = imgui_layer->add_panel<CameraPanel>();
        camera_panel.set_camera(&m_camera_layer->get_camera());

        imgui_layer->add_panel<RendererSettingsPanel>();
        imgui_layer->add_panel<PhysicsSettingsPanel>();
        imgui_layer->add_panel<AudioSettingsPanel>();
        imgui_layer->add_panel<AssetPanel>();
        imgui_layer->add_panel<ViewportPanel>();

        push_overlay(imgui_layer);
        ID_INFO("[Sandbox] DevGUI (ImGuiLayer) 入栈完成");

        ID_INFO("[Sandbox] 构造完成");
    }

private:
    CameraLayer* m_camera_layer = nullptr;
    SceneLayer* m_scene_layer = nullptr;
    ::RenderLayer* m_render_layer = nullptr;
};

ID::Application* ID::create_application()
{
    return new Sandbox();
}
