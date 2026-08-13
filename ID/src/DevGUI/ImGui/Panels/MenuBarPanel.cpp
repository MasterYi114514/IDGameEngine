#include "DevGUI/ImGui/Panels/MenuBarPanel.hpp"

#include "Application/Application.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Log/Log.hpp"

#include "IDJson.hpp"

#include <fstream>
#include <filesystem>

namespace
{
    // 场景保存/加载使用的固定路径（设计书未要求文件对话框，先使用固定路径）
    constexpr const char* k_scene_file = "scenes/save.json";
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

        std::filesystem::create_directories("scenes");

        ArenaID arena = ArenaManager::create_arena();
        const Json json = scene.serialize(arena);
        JSON::write_to_file(k_scene_file, json);
        ArenaManager::destroy_arena(arena);

        ID_INFO("[MenuBar] 场景 '{}' 已保存到 {}", scene.get_name(), k_scene_file);
    }

    void MenuBarPanel::open_scene_from_file()
    {
        std::ifstream file(k_scene_file, std::ios::binary);
        if(!file.good())
        {
            ID_ERROR("[MenuBar] 打开场景失败：找不到文件 {}", k_scene_file);
            return;
        }

        const std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        ArenaID arena = ArenaManager::create_arena();
        const Json json = JSON::parse(content, arena);

        // 创建新场景并反序列化（旧场景保留在场景池中，只是暂停运行）
        Scene& scene = SceneManager::create_scene("Loaded Scene");
        scene.deserialize(json);
        ArenaManager::destroy_arena(arena);

        SceneManager::load_scene(scene);
        ID_INFO("[MenuBar] 已从 {} 加载场景 '{}' ({} 个 GameObject)",
            k_scene_file, scene.get_name(), scene.get_game_object_count());
    }
} // namespace ID
