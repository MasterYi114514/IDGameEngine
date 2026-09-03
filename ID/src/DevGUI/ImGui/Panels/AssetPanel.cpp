#include "DevGUI/ImGui/Panels/AssetPanel.hpp"

#include "Scene/AssetManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"
#include "Renderer/Material/MaterialReflection.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "DevGUI/ImGui/Widgets/MaterialParamWidget.hpp"
#include "Log/Log.hpp"

#include <cstring>

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
        render_mesh_section();
        render_material_section();
        render_loaded_list();
        render_rename_popup();

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

    void AssetPanel::render_drag_list(const char* payload_type,
        const std::vector<std::string>& files, const std::string& category)
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
            if(!category.empty() && ImGui::BeginPopupContextItem(("##ctx_rename_" + category + "_" + file).c_str()))
            {
                if(ImGui::MenuItem("重命名"))
                {
                    open_rename_popup({ category, file, false });
                    ImGui::CloseCurrentPopup();     // 点击后关闭右键菜单
                }
                ImGui::EndPopup();
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

        render_drag_list("ASSET_AUDIO", files, "audio");
    }

    // =====================================================================
    //  Scene 区（★ Save / Load 键）
    // =====================================================================

    void AssetPanel::render_scene_section()
    {
        if(!ImGui::CollapsingHeader("Scene")) return;

        // Load 键：从 Assets/scene/ 选中 .json 加载并切换场景
        // （走 AssetManager：自动保存旧材质库，并优先恢复新场景文件中的材质库；
        //   场景的 Save 在 Scene Settings 面板的场景池列表中，每个场景独立保存）
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

        render_drag_list("ASSET_SCENE", files, "");
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

        render_drag_list("ASSET_SHADER", files, "shader");
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
                TextureID id = AssetManager::load_texture(selected, false);
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

        // .hdr 选中时提示预览特性（float 值直接 Image 显示会 clamp，属预期非 bug）
        if(selected.size() > 4 && selected.substr(selected.size() - 4) == ".hdr")
        {
            ImGui::TextDisabled("HDR 预览为 clamp 后近似（>1 值截断到显示域）");
        }

        render_drag_list("ASSET_TEXTURE", files, "texture");
    }

    // =====================================================================
    //  Mesh 区
    // =====================================================================

    void AssetPanel::render_mesh_section()
    {
        if(!ImGui::CollapsingHeader("Mesh")) return;

        const std::vector<std::string> files = AssetManager::list_meshes();
        const std::string selected = render_combo("##mesh_combo", files, m_selected_mesh_name);

        ImGui::SameLine();
        if(ImGui::Button("Load##mesh"))
        {
            if(selected.empty())
            {
                ID_WARN("AssetPanel：请先选择 Mesh 文件");
            }
            else
            {
                MeshID id = AssetManager::load_mesh(selected);
                if(id.is_valid())
                {
                    add_loaded_asset("mesh", selected,
                        std::string(AssetManager::MeshDir) + selected, id.get_id());
                }
                else
                {
                    ID_ERROR("AssetPanel：Mesh 加载失败: {}", selected);
                }
            }
        }

        render_drag_list("ASSET_MESH", files, "mesh");
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
                    const bool is_new = !MaterialLibrary::contains(mat_name);
                    Material* mat = MaterialLibrary::add(shader_id, mat_name);
                    if(is_new)
                    {
                        // 新材质按 shader 反射填充默认参数；纹理由用户显式拖拽绑定（无纹理时采样默认黑色）
                        for(const EditableParamDesc& desc : get_editable_params(shader_id))
                        {
                            mat->set_param(desc.name, make_default_param(desc));
                        }
                    }
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

            // 第一行：材质名 + shader 文件名 + 删除按钮
            const std::string vs_path = ShaderManager::get_vertex_shader_path(mat->get_shader());
            const std::string vs_name = std::filesystem::path(vs_path).filename().string();
            ImGui::BulletText("%s (shader: %s)", mat->get_name().c_str(), vs_name.c_str());

            // 右键菜单：重命名材质（材质库材质走 MaterialLibrary，不碰磁盘）
            // 注意：BulletText 无 ID，PopupContextItem 必须传显式 str_id，否则所有条目共享同一 popup ID
            if(ImGui::BeginPopupContextItem(("##ctx_rename_mat_" + mat->get_name()).c_str()))
            {
                if(ImGui::MenuItem("重命名"))
                {
                    open_rename_popup({ "material", mat->get_name(), true });
                    ImGui::CloseCurrentPopup();     // 点击后关闭右键菜单
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            if(ImGui::SmallButton(("X##del_mat_" + mat->get_name()).c_str()))
            {
                // 先拷贝名字：remove 会释放 Material，之后 mat 是悬垂指针，不能再访问
                const std::string mat_name = mat->get_name();

                // 删除前检查场景引用：有物体使用该材质时拒绝删除，避免 MaterialInstance::m_parent 悬垂
                // （按 MeshRenderer 池遍历，避免全场景扫描）
                Scene& scene = SceneManager::get_current_scene();
                auto& mesh_pool = scene.get_component_registry().pool<MeshRendererComponent>();
                bool in_use = false;
                for (size_t i = 0; i < mesh_pool.size(); ++i)
                {
                    GameObject::ID go_id = mesh_pool.owners()[i];
                    if (!scene.is_game_object_valid(go_id)) continue;

                    const auto* mesh = scene.get_game_object(go_id).get_component<MeshRendererComponent>();
                    if(mesh && mesh->get_model().get_material().get_parent() == mat)
                    {
                        in_use = true;
                        break;
                    }
                }

                if(in_use)
                {
                    ID_WARN("AssetPanel：材质 '{}' 正被场景物体引用，无法删除", mat_name);
                }
                else
                {
                    MaterialLibrary::remove(mat_name);
                    ID_INFO("AssetPanel：已删除材质 '{}'", mat_name);
                    break;      // mat 已释放，跳出本帧循环
                }
            }

            // 参数编辑子区（默认收起，避免材质列表过长）：反射 shader 全部可编辑参数
            if(ImGui::TreeNode(("参数##mat_params_" + mat->get_name()).c_str()))
            {
                const std::vector<EditableParamDesc> editable =
                    get_editable_params(mat->get_shader());

                // shader 现存参数名集合（供陈旧参数判断）
                std::set<std::string> shader_param_names;
                for(const EditableParamDesc& desc : editable)
                {
                    shader_param_names.insert(desc.name);
                }

                for(const EditableParamDesc& desc : editable)
                {
                    ImGui::PushID(desc.name.c_str());

                    // 当前值：材质已有该参数 → 取之；没有 → 类型默认值（仅显示，改动才写入材质）
                    const auto& defaults = mat->get_param_defaults();
                    const auto param_it = defaults.find(desc.name);
                    MaterialParam param = (param_it != defaults.end()) ? param_it->second
                        : make_default_param(desc);
                    param.type = desc.type;    // 类型以 shader 反射为准，纠正旧数据类型漂移

                    ImGui::TextUnformatted(desc.name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", material_param_type_tag(desc.type));
                    ImGui::SameLine();
                    if(draw_material_param_editor(param, desc.is_color))
                    {
                        mat->set_param(desc.name, param);   // 仅控件修改时写回
                    }

                    // 右键菜单：移除参数（风格参照本文件材质右键重命名菜单，PushID 已隔离 ID）
                    if(ImGui::BeginPopupContextItem("##ctx_remove_param"))
                    {
                        if(ImGui::MenuItem("移除参数"))
                        {
                            mat->remove_param(desc.name);
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                // 材质中存在但 shader 已不存在的陈旧参数：警告 + 移除按钮
                // （先收集名字再逐个渲染，避免边遍历 map 边 remove 导致迭代器失效）
                std::vector<std::string> stale_names;
                for(const auto& [pname, pparam] : mat->get_param_defaults())
                {
                    if(pparam.is_valid() && shader_param_names.find(pname) == shader_param_names.end())
                    {
                        stale_names.push_back(pname);
                    }
                }
                for(const std::string& pname : stale_names)
                {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s (shader 中已不存在)", pname.c_str());
                    ImGui::SameLine();
                    if(ImGui::SmallButton(("移除##stale_" + pname).c_str()))
                    {
                        mat->remove_param(pname);
                    }
                }

                ImGui::TreePop();
            }
        }

        ImGui::Separator();

        // 材质库随场景自动保存/加载（save/load scene 时自动同步到 Assets/material/）；
        // 下方 Save Library / Load Library 用于手动管理材质库文件（如跨场景复用）
        ImGui::TextDisabled("材质库文件（场景 Save/Load 时自动同步同名文件）:");
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

            // 右键菜单：重命名资源（磁盘文件走 AssetManager::rename_asset）
            // 注意：BulletText 无 ID，PopupContextItem 必须传显式 str_id（category+name 保证唯一）
            if(ImGui::BeginPopupContextItem(("##ctx_rename_asset_" + asset.category + "_" + asset.name).c_str()))
            {
                if(ImGui::MenuItem("重命名"))
                {
                    open_rename_popup({ asset.category, asset.name, false });
                    ImGui::CloseCurrentPopup();     // 点击后关闭右键菜单
                }
                ImGui::EndPopup();
            }

            const char* payload_type = nullptr;
            if(asset.category == "audio")       payload_type = "ASSET_AUDIO";
            else if(asset.category == "shader") payload_type = "ASSET_SHADER";
            else if(asset.category == "texture") payload_type = "ASSET_TEXTURE";
            else if(asset.category == "mesh")  payload_type = "ASSET_MESH";

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

    // =====================================================================
    //  改名弹窗（材质库材质 / 磁盘文件）
    // =====================================================================

    void AssetPanel::open_rename_popup(const RenameTarget& target)
    {
        m_rename_target = target;
        // 预填旧名字作为默认新名字，方便用户微调
        std::memset(m_rename_buffer, 0, sizeof(m_rename_buffer));
        std::strncpy(m_rename_buffer, target.old_name.c_str(), sizeof(m_rename_buffer) - 1);
        // 注意：此处位于右键菜单（popup window）上下文中，直接 OpenPopup 会因 ID 栈不同而无法被
        // 主窗口的 BeginPopupModal 匹配；改为请求标志，由 render_rename_popup 在主窗口上下文中打开
        m_rename_open_requested = true;
    }

    void AssetPanel::render_rename_popup()
    {
        // 在主窗口（Asset Browser）上下文中打开弹窗，保证与 BeginPopupModal 的 ID 一致
        if(m_rename_open_requested)
        {
            ImGui::OpenPopup("Rename Asset");
            m_rename_open_requested = false;
        }

        if(m_rename_target.category.empty()) return;

        const bool open = ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        if(!open)
        {
            // 用户按 Esc 或点击外部关闭：清空待改名状态
            m_rename_target.category.clear();
            return;
        }

        ImGui::Text("重命名 %s", m_rename_target.old_name.c_str());
        const bool enter_pressed = ImGui::InputText("##rename_input", m_rename_buffer, sizeof(m_rename_buffer),
            ImGuiInputTextFlags_EnterReturnsTrue);

        bool confirmed = enter_pressed;
        if(ImGui::Button("确认##rename_confirm"))
        {
            confirmed = true;
        }
        ImGui::SameLine();
        if(ImGui::Button("取消##rename_cancel"))
        {
            m_rename_target.category.clear();
            ImGui::CloseCurrentPopup();
        }

        if(confirmed)
        {
            const std::string new_name = m_rename_buffer;
            if(new_name.empty())
            {
                ID_WARN("AssetPanel：重命名失败，新名字不能为空");
            }
            else if(m_rename_target.is_material)
            {
                // 材质库材质：重名拒绝，成功后只改内存名字（序列化保存时自动取新名）
                if(MaterialLibrary::contains(new_name))
                {
                    ID_WARN("AssetPanel：材质重命名失败，'{}' 已存在", new_name);
                }
                else
                {
                    Material* mat = MaterialLibrary::get(m_rename_target.old_name);
                    if(mat)
                    {
                        mat->set_name(new_name);
                        ID_INFO("AssetPanel：材质 '{}' 已重命名为 '{}'",
                            m_rename_target.old_name, new_name);
                        m_rename_target.category.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ID_ERROR("AssetPanel：材质 '{}' 不存在", m_rename_target.old_name);
                    }
                }
            }
            else if(AssetManager::rename_asset(m_rename_target.category,
                m_rename_target.old_name, new_name))
            {
                // 磁盘文件改名成功：同步更新已加载资源列表的 name / path
                for(LoadedAsset& asset : m_loaded_assets)
                {
                    if(asset.category != m_rename_target.category) continue;
                    if(asset.name != m_rename_target.old_name) continue;
                    asset.name = new_name;
                    if(asset.category == "shader")
                    {
                        asset.path = std::string(AssetManager::ShaderDir) + new_name + ".vsl / .fsl";
                    }
                    else if(asset.category == "audio")
                    {
                        asset.path = std::string(AssetManager::AudioDir) + new_name;
                    }
                    else if(asset.category == "texture")
                    {
                        asset.path = std::string(AssetManager::TextureDir) + new_name;
                    }
                    else if(asset.category == "mesh")
                    {
                        asset.path = std::string(AssetManager::MeshDir) + new_name;
                    }
                }

                // 同步刷新对应分区 Combo 选择缓存
                if(m_rename_target.category == "audio" && m_selected_audio_name == m_rename_target.old_name)
                {
                    m_selected_audio_name = new_name;
                }
                if(m_rename_target.category == "shader" && m_selected_shader_name == m_rename_target.old_name)
                {
                    m_selected_shader_name = new_name;
                }
                if(m_rename_target.category == "texture" && m_selected_texture_name == m_rename_target.old_name)
                {
                    m_selected_texture_name = new_name;
                }
                if(m_rename_target.category == "mesh" && m_selected_mesh_name == m_rename_target.old_name)
                {
                    m_selected_mesh_name = new_name;
                }

                ID_INFO("AssetPanel：{} '{}' 已重命名为 '{}'（已加载引用需重新加载）",
                    m_rename_target.category, m_rename_target.old_name, new_name);
                m_rename_target.category.clear();
                ImGui::CloseCurrentPopup();
            }
            // 磁盘文件改名失败（目标已存在 / 非法名字等）：buffer 保留、弹窗不关闭，允许用户修改再试
        }

        ImGui::EndPopup();
    }
} // namespace ID
