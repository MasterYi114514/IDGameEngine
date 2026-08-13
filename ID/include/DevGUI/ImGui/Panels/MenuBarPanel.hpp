#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

#include <vector>

namespace ID
{
    /**
     *  MenuBarPanel — 顶部菜单栏
     *
     *  渲染在 ImGui::BeginMainMenuBar() 中（而非独立窗口）。
     *  设计书注：MenuBar 渲染在 MenuBar 而非窗口中，可不必继承 ImGuiPanel；
     *  但为了能被 ImGuiLayer::add_panel<MenuBarPanel>() 统一管理（见设计书 Sandbox 集成示例），
     *  此处继承 ImGuiPanel，on_imgui_render() 中不使用 begin_window()。
     *  - File：New Scene / Open / Save / Exit
     *  - View：勾选切换各 Panel 的可见性
     *  - Help：About / ImGui Demo
     */
    class ID_API MenuBarPanel : public ImGuiPanel
    {
    public:
        MenuBarPanel() : ImGuiPanel("MenuBar", true) { }

        // 设置可切换的面板列表（由 ImGuiLayer 每帧同步）
        void set_panels(std::vector<ImGuiPanel*> panels) { m_registered_panels = std::move(panels); }

        void on_imgui_render() override;

    private:
        void render_file_menu();
        void render_view_menu();
        void render_help_menu();
        void render_about_window();

        // 场景序列化（Save / Open）
        void save_current_scene();
        void open_scene_from_file();

    private:
        std::vector<ImGuiPanel*> m_registered_panels;
        bool m_show_about = false;
        bool m_show_demo  = false;
    };
} // namespace ID
