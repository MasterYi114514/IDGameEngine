#include "DevGUI/ImGui/Panels/SceneSettingsPanel.hpp"

#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Log/Log.hpp"

namespace ID
{
    SceneSettingsPanel::SceneSettingsPanel() : ImGuiPanel("Scene Settings", true) { }

    void SceneSettingsPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        render_scene_info();
        ImGui::Separator();
        render_scene_pool();

        end_window();
    }

    void SceneSettingsPanel::render_scene_info()
    {
        Scene& scene = SceneManager::get_current_scene();

        // 场景名称编辑
        char name_buf[128];
        std::strncpy(name_buf, scene.get_name().c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        if(ImGui::InputText("Scene Name", name_buf, sizeof(name_buf)))
        {
            scene.set_name(name_buf);
            remember_scene_name(scene.get_name());
        }

        // Running / Paused 状态切换
        bool running = scene.get_is_running();
        if(ImGui::Checkbox("Running", &running))
        {
            if(running) scene.set_running();
            else        scene.set_paused();
        }

        ImGui::TextDisabled("GameObjects: %zu", scene.get_game_object_count());
    }

    void SceneSettingsPanel::render_scene_pool()
    {
        ImGui::Text("Scene Pool:");

        // 当前场景始终在列表中
        remember_scene_name(SceneManager::get_current_scene().get_name());

        for(const std::string& name : m_scene_names)
        {
            ImGui::BulletText("%s", name.c_str());

            const bool is_current = (SceneManager::get_current_scene().get_name() == name);

            ImGui::SameLine();
            if(ImGui::SmallButton(("Load##" + name).c_str()) && !is_current)
            {
                // create_scene 对已存在的名称返回已有场景，因此 Load 是安全的
                Scene& scene = SceneManager::create_scene(name);
                SceneManager::load_scene(scene);
                ID_INFO("[SceneSettings] 已切换到场景 '{}'", name);
            }

            ImGui::SameLine();
            if(ImGui::SmallButton(("Destroy##" + name).c_str()) && !is_current)
            {
                Scene& scene = SceneManager::create_scene(name);
                SceneManager::destroy_scene(scene);
                ID_INFO("[SceneSettings] 已销毁场景 '{}'", name);
            }
        }

        // 创建新场景
        ImGui::Separator();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("##new_scene", m_new_scene_name, sizeof(m_new_scene_name));
        ImGui::SameLine();
        if(ImGui::Button("+ Create New Scene"))
        {
            const std::string name = m_new_scene_name;
            if(!name.empty())
            {
                Scene& scene = SceneManager::create_scene(name);
                SceneManager::load_scene(scene);
                remember_scene_name(name);
                ID_INFO("[SceneSettings] 已创建并切换到场景 '{}'", name);
            }
        }
    }

    void SceneSettingsPanel::remember_scene_name(const std::string& name)
    {
        if(name.empty()) return;

        const auto it = std::find(m_scene_names.begin(), m_scene_names.end(), name);
        if(it == m_scene_names.end())
        {
            m_scene_names.push_back(name);
        }
    }
} // namespace ID
