#include "DevGUI/ImGui/Panels/SceneSettingsPanel.hpp"

#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/AssetManager.hpp"
#include "Log/Log.hpp"

namespace
{
    // 由场景名生成安全文件名（清理 Windows 非法字符），返回 "<名>.json"
    std::string scene_filename(const std::string& name)
    {
        std::string filename = name;
        for(char& c : filename)
        {
            if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
                || c == '"' || c == '<' || c == '>' || c == '|')
            {
                c = '_';
            }
        }
        filename += ".json";
        return filename;
    }
} // 匿名命名空间

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

        // 场景名称编辑（名字只是名字，修改不影响场景身份，无需记入池列表）
        char name_buf[128];
        std::strncpy(name_buf, scene.get_name().c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        if(ImGui::InputText("Scene Name", name_buf, sizeof(name_buf)))
        {
            scene.set_name(name_buf);
        }

        // Running / Paused 状态切换
        bool running = scene.get_is_running();
        if(ImGui::Checkbox("Running", &running))
        {
            if(running) scene.set_running();
            else        scene.set_paused();
        }

        ImGui::TextDisabled("GameObjects: %zu", scene.get_game_object_count());
        ImGui::TextDisabled("SceneID: %u", scene.get_id().id);
    }

    void SceneSettingsPanel::render_scene_pool()
    {
        ImGui::Text("Scene Pool:");

        // 当前场景始终在列表中（按 ID 去重，改名不会产生重复项）
        remember_scene(SceneManager::get_current_scene());

        // 先清理已销毁场景的残留 ID（避免列表出现幽灵项）
        for(auto it = m_scene_ids.begin(); it != m_scene_ids.end();)
        {
            if(!SceneManager::find_scene(*it))
            {
                it = m_scene_ids.erase(it);
            }
            else
            {
                ++it;
            }
        }

        const Scene& current = SceneManager::get_current_scene();

        for(const SceneID& id : m_scene_ids)
        {
            Scene* scene = SceneManager::find_scene(id);
            if(!scene) continue;

            ImGui::BulletText("%s (id=%u)", scene->get_name().c_str(), id.id);

            const bool is_current = (current.get_id() == id);
            const std::string btn_suffix = std::to_string(id.id);

            ImGui::SameLine();
            if(ImGui::SmallButton(("Load##" + btn_suffix).c_str()) && !is_current)
            {
                SceneManager::load_scene(*scene);
                ID_INFO("[SceneSettings] 已切换到场景 '{}'", scene->get_name());
            }

            // Save：保存该场景到 Assets/scene/<场景名>.json
            ImGui::SameLine();
            if(ImGui::SmallButton(("Save##" + btn_suffix).c_str()))
            {
                const std::string filepath = std::string(AssetManager::SceneDir) + scene_filename(scene->get_name());
                SceneManager::save(*scene, filepath);
                ID_INFO("[SceneSettings] 场景 '{}' 已保存到 {}", scene->get_name(), filepath);
            }

            ImGui::SameLine();
            if(ImGui::SmallButton(("Destroy##" + btn_suffix).c_str()) && !is_current)
            {
                SceneManager::destroy_scene(*scene);
                ID_INFO("[SceneSettings] 已销毁场景 '{}'", scene->get_name());
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
                remember_scene(scene);
                ID_INFO("[SceneSettings] 已创建并切换到场景 '{}' (id={})", name, scene.get_id().id);
            }
        }
    }

    void SceneSettingsPanel::remember_scene(const Scene& scene)
    {
        const auto it = std::find(m_scene_ids.begin(), m_scene_ids.end(), scene.get_id());
        if(it == m_scene_ids.end())
        {
            m_scene_ids.push_back(scene.get_id());
        }
    }
} // namespace ID
