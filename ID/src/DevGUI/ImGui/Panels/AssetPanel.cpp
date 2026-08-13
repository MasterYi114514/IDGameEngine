#include "DevGUI/ImGui/Panels/AssetPanel.hpp"

#include "Scene/AssetManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Log/Log.hpp"

namespace ID
{
    AssetPanel::AssetPanel() : ImGuiPanel("Asset Browser", true) { }

    void AssetPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        render_audio_section();
        render_scene_section();
        render_shader_section();
        render_texture_section();
        render_material_section();
        render_loaded_list();

        end_window();
    }

    // =====================================================================
    //  通用辅助：Combo 下拉 + 可拖拽目录列表
    // =====================================================================

    std::string AssetPanel::render_combo(const char* combo_id,
        const std::vector<std::string>& files, std::string& selected_name)
    {
        // 选中项不在最新列表时重置（如文件被删除）
        if(!selected_name.empty())
        {
            const auto it = std::find(files.begin(), files.end(), selected_name);
            if(it == files.end()) selected_name.clear();
        }

        const char* preview = selected_name.empty() ? "选择资源..." : selected_name.c_str();
        ImGui::SetNextItemWidth(200.0f);
        if(ImGui::BeginCombo(combo_id, preview))
        {
            for(const auto& file : files)
            {
                const bool selected = (file == selected_name);
                if(ImGui::Selectable(file.c_str(), selected))
                {
                    selected_name = file;
                }
            }
            ImGui::EndCombo();
        }
        return selected_name;
    }

    void AssetPanel::render_drag_list(const char* payload_type, const std::vector<std::string>& files)
    {
        ImGui::TextDisabled("目录列表（拖拽到 Inspector 选择资源）:");
        for(const auto& file : files)
        {
            ImGui::BulletText("%s", file.c_str());
            if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload(payload_type, file.c_str(), file.size() + 1);
                ImGui::Text("拖拽 %s", file.c_str());
                ImGui::EndDragDropSource();
            }
        }
    }

    // =====================================================================
    //  Audio 区
    // =====================================================================

    void AssetPanel::render_audio_section()
    {
        if(!ImGui::CollapsingHeader("Audio")) return;

        const std::vector<std::string> files = AssetManager::list_audios();
        const std::string selected = render_combo("##audio_combo", files, m_selected_audio_name);

        ImGui::SameLine();
        if(ImGui::Button("Load##audio"))
        {
            if(selected.empty())
            {
                ID_WARN("AssetPanel：请先选择音频文件");
            }
            else
            {
                AudioID id = AssetManager::load_audio(selected);
                if(id.is_valid())
                {
                    add_loaded_asset("audio", selected, std::string(AssetManager::AudioDir) + selected, id.id);
                }
                else
                {
                    ID_ERROR("AssetPanel：音频加载失败: {}", selected);
                }
            }
        }

        render_drag_list("ASSET_AUDIO", files);
    }

    // =====================================================================
    //  Scene 区（★ Save / Load 键）
    // =====================================================================

    void AssetPanel::render_scene_section()
    {
        if(!ImGui::CollapsingHeader("Scene")) return;

        // Load 键：从 Assets/scene/ 选中 .json 加载并切换场景
        // （场景的 Save 在 Scene Settings 面板的场景池列表中，每个场景独立保存）
        const std::vector<std::string> files = AssetManager::list_scenes();
        const std::string selected = render_combo("##scene_load_combo", files, m_selected_scene_name);

        ImGui::SameLine();
        if(ImGui::Button("Load##scene"))
        {
            if(selected.empty())
            {
                ID_WARN("AssetPanel：请先选择场景文件");
            }
            else
            {
                Scene& scene = AssetManager::load_scene(selected);
                SceneManager::load_scene(scene);
                ID_INFO("AssetPanel：已加载并切换场景 '{}'", scene.get_name());
            }
        }

        render_drag_list("ASSET_SCENE", files);
    }

    // =====================================================================
    //  Shader 区
    // =====================================================================

    void AssetPanel::render_shader_section()
    {
        if(!ImGui::CollapsingHeader("Shader")) return;

        const std::vector<std::string> files = AssetManager::list_shaders();
        const std::string selected = render_combo("##shader_combo", files, m_selected_shader_name);

        ImGui::SameLine();
        if(ImGui::Button("Load##shader"))
        {
            if(selected.empty())
            {
                ID_WARN("AssetPanel：请先选择 shader");
            }
            else
            {
                ShaderID id = AssetManager::load_shader(selected);
                if(id.is_valid())
                {
                    add_loaded_asset("shader", selected,
                        std::string(AssetManager::ShaderDir) + selected + ".vsl / .fsl", id.get_id());
                }
                else
                {
                    ID_ERROR("AssetPanel：shader 加载失败: {}", selected);
                }
            }
        }

        render_drag_list("ASSET_SHADER", files);
    }

    // =====================================================================
    //  Texture 区
    // =====================================================================

    void AssetPanel::render_texture_section()
    {
        if(!ImGui::CollapsingHeader("Texture")) return;

        const std::vector<std::string> files = AssetManager::list_textures();
        const std::string selected = render_combo("##texture_combo", files, m_selected_texture_name);

        ImGui::SameLine();
        if(ImGui::Button("Load##texture"))
        {
            if(selected.empty())
            {
                ID_WARN("AssetPanel：请先选择纹理文件");
            }
            else
            {
                TextureID id = AssetManager::load_texture(selected);
                if(id.is_valid())
                {
                    add_loaded_asset("texture", selected,
                        std::string(AssetManager::TextureDir) + selected, id.get_id());
                }
                else
                {
                    ID_ERROR("AssetPanel：纹理加载失败: {}", selected);
                }
            }
        }

        render_drag_list("ASSET_TEXTURE", files);
    }

    // =====================================================================
    //  Material 区（运行时材质库管理：创建 / 查看 / 删除）
    // =====================================================================

    void AssetPanel::render_material_section()
    {
        if(!ImGui::CollapsingHeader("Material")) return;

        // 创建新材质：选 shader + 输入名称
        const std::vector<std::string> shaders = AssetManager::list_shaders();
        const std::string selected_shader = render_combo("##material_shader_combo", shaders, m_selected_material_shader);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##material_name", m_material_name, sizeof(m_material_name));

        if(ImGui::Button("Create Material"))
        {
            const std::string mat_name = m_material_name;
            if(mat_name.empty() || selected_shader.empty())
            {
                ID_WARN("AssetPanel：创建材质需要非空的名称和 shader");
            }
            else
            {
                ShaderID shader_id = AssetManager::load_shader(selected_shader);
                if(shader_id.is_valid())
                {
                    Material* mat = MaterialLibrary::add(shader_id, mat_name);
                    ID_INFO("AssetPanel：已创建材质 '{}'（shader: {}）", mat_name, selected_shader);
                }
                else
                {
                    ID_ERROR("AssetPanel：shader 加载失败: {}", selected_shader);
                }
            }
        }

        // 材质库列表（名称 + shader 文件名 + 删除）
        ImGui::TextDisabled("材质库（Inspector 的 Material 下拉可选用）:");
        const std::vector<Material*> materials = MaterialLibrary::get_all();
        if(materials.empty())
        {
            ImGui::BulletText("（暂无材质，请用上方创建）");
        }
        for(Material* mat : materials)
        {
            if(!mat) continue;

            // 显示材质名 + 顶点 shader 文件名
            const std::string vs_path = ShaderManager::get_vertex_shader_path(mat->get_shader());
            const std::string vs_name = std::filesystem::path(vs_path).filename().string();
            ImGui::BulletText("%s (shader: %s)", mat->get_name().c_str(), vs_name.c_str());

            ImGui::SameLine();
            if(ImGui::SmallButton(("X##del_mat_" + mat->get_name()).c_str()))
            {
                MaterialLibrary::remove(mat->get_name());
                ID_INFO("AssetPanel：已删除材质 '{}'", mat->get_name());
            }
        }

        ImGui::Separator();

        // 全局材质库持久化：Save（全库 → 文件） / Load（文件 → 全库）
        ImGui::TextDisabled("全局材质库（MaterialLibrary 属于全体，不随场景序列化）:");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##material_lib_file", m_material_lib_name, sizeof(m_material_lib_name));
        ImGui::SameLine();
        if(ImGui::Button("Save Library"))
        {
            const std::string filename = m_material_lib_name;
            if(filename.empty())
            {
                ID_WARN("AssetPanel：材质库文件名不能为空");
            }
            else
            {
                AssetManager::save_material_library(filename);
            }
        }

        const std::vector<std::string> libs = AssetManager::list_material_libraries();
        const std::string selected_lib = render_combo("##material_lib_combo", libs, m_selected_material_lib);
        ImGui::SameLine();
        if(ImGui::Button("Load Library"))
        {
            if(selected_lib.empty())
            {
                ID_WARN("AssetPanel：请先选择材质库文件");
            }
            else
            {
                AssetManager::load_material_library(selected_lib);
            }
        }
    }

    // =====================================================================
    //  已加载资源列表（每项可作为拖拽源）
    // =====================================================================

    void AssetPanel::render_loaded_list()
    {
        ImGui::Separator();
        if(!ImGui::CollapsingHeader("Loaded Assets")) return;

        if(m_loaded_assets.empty())
        {
            ImGui::TextDisabled("（暂无已加载资源）");
            return;
        }

        for(const auto& asset : m_loaded_assets)
        {
            ImGui::BulletText("[%s] #%u %s (%s)", asset.category.c_str(), asset.id,
                asset.name.c_str(), asset.path.c_str());

            const char* payload_type = nullptr;
            if(asset.category == "audio")       payload_type = "ASSET_AUDIO";
            else if(asset.category == "shader") payload_type = "ASSET_SHADER";
            else if(asset.category == "texture") payload_type = "ASSET_TEXTURE";

            if(payload_type && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload(payload_type, asset.name.c_str(), asset.name.size() + 1);
                ImGui::Text("拖拽 %s", asset.name.c_str());
                ImGui::EndDragDropSource();
            }
        }
    }

    void AssetPanel::add_loaded_asset(const std::string& category, const std::string& name,
        const std::string& path, uint32_t id)
    {
        m_loaded_assets.push_back({ category, name, path, id });
        ID_INFO("AssetPanel：已加载 {} '{}' (id={}, {})", category, name, id, path);
    }
} // namespace ID
