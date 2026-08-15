#include "DevGUI/ImGui/Panels/MenuBarPanel.hpp"

#include "Application/Application.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/AssetManager.hpp"
#include "Log/Log.hpp"

namespace
{
    // 菜单 Save/Open 使用的固定文件名（AssetManager 内部拼到 Assets/scene/ 下）
    constexpr const char* k_scene_file = "save.json";
} // 匿名命名空间

namespace ID
{
    void MenuBarPanel::on_imgui_render()
    {
        if(!ImGui::BeginMainMenuBar())
        {
            return;
        }

        render_file_menu();
        render_view_menu();
        render_help_menu();

        ImGui::EndMainMenuBar();

        render_about_window();
    }

    // =====================================================================
    //  File 菜单
    // =====================================================================

    void MenuBarPanel::render_file_menu()
    {
        if(!ImGui::BeginMenu("File"))
        {
            return;
        }

        if(ImGui::MenuItem("New Scene"))
        {
            // 创建新场景并立即切换（每个场景有唯一 SceneID，重名互不影响）
            Scene& scene = SceneManager::create_scene("New Scene");
            SceneManager::load_scene(scene);
            ID_INFO("[MenuBar] 已创建并切换到新场景 '{}'", scene.get_name());
        }
        if(ImGui::MenuItem("Open..."))
        {
            open_scene_from_file();
        }
        if(ImGui::MenuItem("Save"))
        {
            save_current_scene();
        }

        ImGui::Separator();

        if(ImGui::MenuItem("Exit"))
        {
            Application::get_instance().close();
        }

        ImGui::EndMenu();
    }

    // =====================================================================
    //  View 菜单：勾选切换各 Panel 可见性
    // =====================================================================

    void MenuBarPanel::render_view_menu()
    {
        if(!ImGui::BeginMenu("View"))
        {
            return;
        }

        for(ImGuiPanel* panel : m_registered_panels)
        {
            if(!panel) continue;

            const bool open = panel->is_open();
            if(ImGui::MenuItem(panel->get_title().c_str(), nullptr, open))
            {
                panel->toggle_open();
            }
        }

        ImGui::EndMenu();
    }

    // =====================================================================
    //  Help 菜单
    // =====================================================================

    void MenuBarPanel::render_help_menu()
    {
        if(!ImGui::BeginMenu("Help"))
        {
            return;
        }

        if(ImGui::MenuItem("About ID Engine"))
        {
            m_show_about = true;
        }
        if(ImGui::MenuItem("ImGui Demo"))
        {
            m_show_demo = true;
        }

        ImGui::EndMenu();
    }

    void MenuBarPanel::render_about_window()
    {
        if(m_show_about)
        {
            ImGui::Begin("About ID Engine", &m_show_about);
            ImGui::Text("ID Game Engine — DevGUI");
            ImGui::Text("Dear ImGui %s (docking branch)", IMGUI_VERSION);
            ImGui::Text("Renderer: OpenGL 4.4 Core");
            ImGui::Text("LayerStack: Camera → Scene → Render → DevGUI(overlay)");
            ImGui::Separator();
            ImGui::Text("Dockspace: 支持面板自由拖拽停靠");
            ImGui::End();
        }

        if(m_show_demo)
        {
            ImGui::ShowDemoWindow(&m_show_demo);
        }
    }

    // =====================================================================
    //  场景序列化（Save / Open）
    // =====================================================================

    void MenuBarPanel::save_current_scene()
    {
        Scene& scene = SceneManager::get_current_scene();
        AssetManager::save_scene(scene, k_scene_file);
    }

    void MenuBarPanel::open_scene_from_file()
    {
        // 走 AssetManager：自动保存旧材质库并恢复新场景的材质库
        Scene& scene = AssetManager::load_scene(k_scene_file);
        SceneManager::load_scene(scene);
        ID_INFO("[MenuBar] 已从 {} 加载场景 '{}' ({} 个 GameObject)",
            k_scene_file, scene.get_name(), scene.get_game_object_count());
    }
} // namespace ID
