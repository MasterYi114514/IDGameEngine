#include "DevGUI/ImGui/Panels/InspectorPanel.hpp"

#include "DevGUI/ImGui/ImGuiLayer.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"
#include "Scene/Component/LightComponent.hpp"
#include "Scene/Component/RigidBodyComponent.hpp"
#include "Scene/Component/AudioSourceComponent.hpp"
#include "Scene/Component/AudioListenerComponent.hpp"
#include "Scene/AssetManager.hpp"
#include "Scene/Audio/AudioManager.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"
#include "Log/Log.hpp"

#include <cstring>

namespace
{
    using namespace ID;

    // 常用图元网格（Mesh 下拉框的候选，选择后创建对应 Mesh）
    struct PrimitiveOption
    {
        const char* label;
        MeshID (*create)();
    };

    MeshID create_cube_mesh()    { return MeshFactory::create_cube(1.0f); }
    MeshID create_cuboid_mesh()  { return MeshFactory::create_cuboid(1.0f, 1.0f, 1.0f); }
    MeshID create_sphere_mesh()  { return MeshFactory::create_sphere(0.5f); }
    MeshID create_plane_mesh()   { return MeshFactory::create_plane(10.0f, 10.0f); }
    MeshID create_cylinder_mesh(){ return MeshFactory::create_cylinder(0.5f, 1.0f); }

    const PrimitiveOption k_primitive_options[] = {
        { "Cube",     create_cube_mesh },
        { "Cuboid",   create_cuboid_mesh },
        { "Sphere",   create_sphere_mesh },
        { "Plane",    create_plane_mesh },
        { "Cylinder", create_cylinder_mesh },
    };

    // 组件 Active 开关：勾选时调用 make_active()（可能因资源检查失败而保持未勾选）
    // 调用方需先用 PushID 隔离 ID 域，避免多个组件的 "Active" 复选框冲突
    template<typename ComponentType>
    void render_component_active_checkbox(ComponentType& component)
    {
        bool active = component.is_active();
        if(ImGui::Checkbox("Active", &active))
        {
            if(active) { component.make_active(); }
            else      { component.make_inactive(); }
        }
    }
} // 匿名命名空间

namespace ID
{
    InspectorPanel::InspectorPanel(ImGuiLayer* imgui_layer)
        : ImGuiPanel("Inspector", true), m_imgui_layer(imgui_layer) { }

    void InspectorPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        // 未选中 / 无场景：显示占位信息
        if(!m_context || m_selected_id == GameObject::INVALID_ID
            || !m_context->is_game_object_valid(m_selected_id))
        {
            ImGui::Text("未选中任何对象");
            ImGui::TextDisabled("在 Scene Hierarchy 中点击节点以检视属性");
            end_window();
            return;
        }

        GameObject& go = m_context->get_game_object(m_selected_id);

        render_game_object_header(go);
        ImGui::Separator();

        // ---- 各 Component 独立折叠块 ----
        if(auto* transform = go.get_component<TransformComponent>())
        {
            render_transform_editor(*transform);
        }
        if(auto* mesh = go.get_component<MeshRendererComponent>())
        {
            render_mesh_renderer_editor(*mesh);
        }
        if(auto* light = go.get_component<LightComponent>())
        {
            render_light_editor(*light);
        }
        if(auto* rigid_body = go.get_component<RigidBodyComponent>())
        {
            render_rigid_body_editor(*rigid_body);
        }
        if(auto* audio = go.get_component<AudioSourceComponent>())
        {
            render_audio_source_editor(*audio);
        }
        if(auto* listener = go.get_component<AudioListenerComponent>())
        {
            render_audio_listener_editor(*listener);
        }

        ImGui::Separator();
        render_add_component_menu(go);

        end_window();
    }

    // =====================================================================
    //  GameObject 基础信息
    // =====================================================================

    void InspectorPanel::render_game_object_header(GameObject& go)
    {
        // Active 开关
        bool active = go.is_active();
        if(ImGui::Checkbox("Active", &active))
        {
            go.set_active(active);
        }

        // 名称编辑
        char name_buf[128];
        std::strncpy(name_buf, go.get_name().c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        if(ImGui::InputText("Name", name_buf, sizeof(name_buf)))
        {
            go.set_name(name_buf);
        }

        // Tag（预留）
        ImGui::Text("Tag:  Untagged (预留)");
        ImGui::TextDisabled("ID: %u", m_selected_id);
    }

    // =====================================================================
    //  Transform 编辑器
    // =====================================================================

    void InspectorPanel::render_transform_editor(TransformComponent& transform)
    {
        ImGui::PushID("transform");
        if(!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PopID();
            return;
        }

        render_component_active_checkbox(transform);

        // Position
        const Pos3& position = transform.get_position();
        float pos[3] = { position[0], position[1], position[2] };
        if(ImGui::DragFloat3("Position", pos, 0.1f))
        {
            transform.set_position(Pos3(pos[0], pos[1], pos[2]));
        }

        // Rotation（四元数 ↔ Euler 角度，方便人类理解）
        const Vec3 euler = transform.get_orientation().to_euler();
        float rot[3] = { euler[0], euler[1], euler[2] };
        if(ImGui::DragFloat3("Rotation", rot, 0.5f))
        {
            transform.set_orientation(Quat::from_euler(rot[0], rot[1], rot[2]));
        }

        // Scale（限制最小 0.001，避免 0/负缩放）
        const Vec3& scale = transform.get_scale();
        float sca[3] = { scale[0], scale[1], scale[2] };
        if(ImGui::DragFloat3("Scale", sca, 0.01f))
        {
            const Vec3 clamped(
                std::max(sca[0], 0.001f),
                std::max(sca[1], 0.001f),
                std::max(sca[2], 0.001f));
            transform.set_scale(clamped);
        }
        ImGui::PopID();
    }

    // =====================================================================
    //  MeshRenderer 编辑器
    // =====================================================================

    void InspectorPanel::render_mesh_renderer_editor(MeshRendererComponent& mesh)
    {
        ImGui::PushID("mesh_renderer");
        if(!ImGui::CollapsingHeader("Mesh Renderer"))
        {
            ImGui::PopID();
            return;
        }

        render_component_active_checkbox(mesh);

        Model& model = mesh.get_model();

        // ---- Mesh 选择（枚举内置图元；文件 Mesh 显示为 File）----
        const MeshID mesh_id = model.get_mesh_id();
        const MeshSourceDesc* source = MeshFactory::get_source_desc(mesh_id);

        int current = -1;
        if(source && source->source_type == MeshSourceType::Primitive)
        {
            current = static_cast<int>(source->primitive_type) - 1; // Primitive 枚举从 Cube=1 开始
        }

        if(ImGui::BeginCombo("Mesh", current >= 0 ? k_primitive_options[current].label : "None/File"))
        {
            for(int i = 0; i < static_cast<int>(std::size(k_primitive_options)); ++i)
            {
                const bool selected = (i == current);
                if(ImGui::Selectable(k_primitive_options[i].label, selected))
                {
                    model.set_mesh(k_primitive_options[i].create());
                }
                if(selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // ---- Material 选择（枚举 MaterialLibrary）----
        const MaterialInstance& material = model.get_material();
        const std::string current_name = material.get_parent()
            ? material.get_parent()->get_name() : "(none)";

        if(ImGui::BeginCombo("Material", current_name.c_str()))
        {
            const std::vector<Material*> materials = MaterialLibrary::get_all();
            for(Material* mat : materials)
            {
                if(!mat) continue;
                const bool selected = (material.get_parent() == mat);
                if(ImGui::Selectable(mat->get_name().c_str(), selected))
                {
                    model.set_material(MaterialInstance(*mat));
                }
                if(selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // ---- Material 区拖拽接收（从 Asset Browser 拖入 shader / 纹理）----
        ImGui::TextDisabled("拖拽 shader / 纹理 到此处选择资源");
        if(ImGui::BeginDragDropTarget())
        {
            if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_SHADER"))
            {
                const std::string name = static_cast<const char*>(payload->Data);
                ShaderID shader_id = AssetManager::load_shader(name);
                if(shader_id.is_valid())
                {
                    // 用新 shader 重建材质并绑定到模型；新材质补默认值（u_color + 纹理），避免全黑
                    const bool is_new = !MaterialLibrary::contains(name);
                    Material* mat = MaterialLibrary::add(shader_id, name);
                    if(is_new)
                    {
                        mat->set_param("u_color", Vec3(1.0f, 1.0f, 1.0f));
                        const std::vector<std::string> textures = AssetManager::list_textures();
                        if(!textures.empty())
                        {
                            TextureID tex = AssetManager::load_texture(textures[0]);
                            if(tex.is_valid())
                            {
                                mat->set_texture("texture_sampler", tex, 0);
                            }
                        }
                    }
                    model.set_material(MaterialInstance(*mat));
                    ID_INFO("[Inspector] 已用 shader '{}' 重建材质", name);
                }
                else
                {
                    ID_ERROR("[Inspector] shader 加载失败: {}", name);
                }
            }
            if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
            {
                const std::string name = static_cast<const char*>(payload->Data);
                TextureID texture_id = AssetManager::load_texture(name);
                if(texture_id.is_valid())
                {
                    MaterialInstance& instance = model.get_material();
                    // binding 名：优先取材质现有第一个 texture binding，否则用约定名（geometry.fsl 的 sampler）
                    std::string binding = "texture_sampler";
                    if(instance.get_parent())
                    {
                        const auto& defaults = instance.get_parent()->get_texture_defaults();
                        if(!defaults.empty()) binding = defaults.begin()->first;
                    }
                    instance.set_texture(binding, texture_id, 0);
                    ID_INFO("[Inspector] 已绑定纹理 '{}' 到材质 binding '{}'", name, binding);
                }
                else
                {
                    ID_ERROR("[Inspector] 纹理加载失败: {}", name);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 引擎当前未提供 per-object 阴影开关，Cast Shadows 暂不展示
        ImGui::TextDisabled("Cast Shadows: 引擎暂未提供 per-object 阴影开关");
        ImGui::PopID();
    }

    // =====================================================================
    //  Light 编辑器
    // =====================================================================

    void InspectorPanel::render_light_editor(LightComponent& light)
    {
        ImGui::PushID("light");
        if(!ImGui::CollapsingHeader("Light"))
        {
            ImGui::PopID();
            return;
        }

        render_component_active_checkbox(light);

        Light& l = light.get_light();

        // Type
        const char* type_names[] = { "Directional", "Point", "Spot" };
        int type_index = static_cast<int>(l.type);
        if(ImGui::Combo("Type", &type_index, type_names, 3))
        {
            l.type = static_cast<LightType>(type_index);
        }

        // Color
        float color[3] = { l.color[0], l.color[1], l.color[2] };
        if(ImGui::ColorEdit3("Color", color))
        {
            l.color = Vec3(color[0], color[1], color[2]);
        }

        // Intensity
        if(ImGui::DragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 100.0f))
        {
            l.intensity = std::max(l.intensity, 0.0f);
        }

        // Enabled
        bool enabled = l.enabled;
        if(ImGui::Checkbox("Enabled", &enabled))
        {
            l.enabled = enabled;
        }

        // Direction / Position（取决于光源类型）
        if(l.type == LightType::Directional)
        {
            float dir[3] = { l.drop.direction[0], l.drop.direction[1], l.drop.direction[2] };
            if(ImGui::DragFloat3("Direction", dir, 0.05f))
            {
                l.drop.direction = Vec3(dir[0], dir[1], dir[2]);
            }
        }
        else
        {
            float pos[3] = { l.drop.position[0], l.drop.position[1], l.drop.position[2] };
            if(ImGui::DragFloat3("Position", pos, 0.05f))
            {
                l.drop.position = Pos3(pos[0], pos[1], pos[2]);
            }

            if(l.type == LightType::Spot)
            {
                if(ImGui::DragFloat("Inner Cone", &l.inner_cone_angle, 0.5f, 0.0f, 90.0f)) { }
                if(ImGui::DragFloat("Outer Cone", &l.outer_cone_angle, 0.5f, 0.0f, 90.0f)) { }
            }
        }
        ImGui::PopID();
    }

    // =====================================================================
    //  RigidBody 编辑器
    // =====================================================================

    void InspectorPanel::render_rigid_body_editor(RigidBodyComponent& rigid_body)
    {
        ImGui::PushID("rigid_body");
        if(!ImGui::CollapsingHeader("RigidBody"))
        {
            ImGui::PopID();
            return;
        }

        render_component_active_checkbox(rigid_body);

        // Type
        const char* type_names[] = { "Static", "Dynamic", "Kinematic" };
        int type_index = static_cast<int>(rigid_body.get_type());
        if(ImGui::Combo("Type", &type_index, type_names, 3))
        {
            rigid_body.set_type(static_cast<RigidBodyType>(type_index));
        }

        // Mass（仅动态刚体有意义，但允许编辑）
        float mass = rigid_body.get_mass();
        if(ImGui::DragFloat("Mass", &mass, 0.05f, 0.01f, 1000.0f))
        {
            rigid_body.set_mass(std::max(mass, 0.01f));
        }

        // Trigger
        bool trigger = rigid_body.is_trigger();
        if(ImGui::Checkbox("Trigger", &trigger))
        {
            rigid_body.set_trigger(trigger);
        }

        // Damping
        float linear_damping = rigid_body.get_linear_damping();
        if(ImGui::DragFloat("Linear Damping", &linear_damping, 0.01f, 0.0f, 10.0f))
        {
            rigid_body.set_liner_damping(std::max(linear_damping, 0.0f));
        }

        float angular_damping = rigid_body.get_angular_damping();
        if(ImGui::DragFloat("Angular Damping", &angular_damping, 0.01f, 0.0f, 10.0f))
        {
            rigid_body.set_angular_damping(std::max(angular_damping, 0.0f));
        }

        // 物理材质
        const PhysicsMaterial& mat = rigid_body.get_material();
        float friction    = mat.friction;
        float restitution = mat.restitution;
        bool  material_changed = false;

        if(ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 2.0f))
        {
            material_changed = true;
        }
        if(ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 2.0f))
        {
            material_changed = true;
        }
        if(material_changed)
        {
            PhysicsMaterial new_mat = mat;
            new_mat.friction    = std::max(friction, 0.0f);
            new_mat.restitution = std::max(restitution, 0.0f);
            rigid_body.set_material(new_mat);
        }
        ImGui::PopID();
    }

    // =====================================================================
    //  AudioSource 编辑器
    // =====================================================================

    void InspectorPanel::render_audio_source_editor(AudioSourceComponent& audio)
    {
        ImGui::PushID("audio_source");
        if(!ImGui::CollapsingHeader("Audio Source"))
        {
            ImGui::PopID();
            return;
        }

        render_component_active_checkbox(audio);

        // Clip 展示：优先显示加载路径，无法反查时显示"已关联"
        const AudioClipID clip_id = audio.get_clip();
        if(clip_id.is_valid())
        {
            const std::string clip_path = AudioManager::get_audio_path_by_clip(clip_id);
            if(!clip_path.empty())
            {
                ImGui::Text("Clip: %s", clip_path.c_str());
            }
            else
            {
                ImGui::Text("Clip: 已关联");
            }
        }
        else
        {
            ImGui::Text("Clip: (无)");
        }

        // 拖拽接收：从 Asset Browser 拖入音频文件选择 Clip
        ImGui::TextDisabled("拖拽音频到此处选择 Clip");
        if(ImGui::BeginDragDropTarget())
        {
            if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_AUDIO"))
            {
                const std::string name = static_cast<const char*>(payload->Data);
                AudioID audio_id = AssetManager::load_audio(name);
                if(audio_id.is_valid())
                {
                    audio.set_clip(AudioManager::get_clip(audio_id));
                    audio.set_clip_path(std::string(AssetManager::AudioDir) + name);
                    ID_INFO("[Inspector] AudioSource 已关联音频 '{}'", name);
                }
                else
                {
                    ID_ERROR("[Inspector] 音频加载失败: {}", name);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Volume
        float volume = audio.get_volume();
        if(ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f))
        {
            audio.set_volume(volume);
        }

        // Pitch
        float pitch = audio.get_pitch();
        if(ImGui::SliderFloat("Pitch", &pitch, 0.1f, 4.0f))
        {
            audio.set_pitch(pitch);
        }

        // Loop
        bool loop = audio.get_loop();
        if(ImGui::Checkbox("Loop", &loop))
        {
            audio.set_loop(loop);
        }

        // Spatial
        bool spatial = audio.is_spatial();
        if(ImGui::Checkbox("Spatial (3D)", &spatial))
        {
            audio.set_spatial(spatial);
        }

        // Play / Pause / Stop
        const bool playing = audio.is_playing();
        ImGui::Text("状态: %s", playing ? "播放中" : "已停止");

        if(ImGui::Button("Play"))
        {
            audio.play(clip_id);
        }
        ImGui::SameLine();
        if(ImGui::Button("Pause"))
        {
            audio.pause();
        }
        ImGui::SameLine();
        if(ImGui::Button("Stop"))
        {
            audio.stop();
        }
        ImGui::PopID();
    }

    // =====================================================================
    //  AudioListener 编辑器（只读）
    // =====================================================================

    void InspectorPanel::render_audio_listener_editor(AudioListenerComponent& listener)
    {
        ImGui::PushID("audio_listener");
        if(!ImGui::CollapsingHeader("Audio Listener"))
        {
            ImGui::PopID();
            return;
        }

        render_component_active_checkbox(listener);

        ImGui::TextDisabled("监听器位置/朝向每帧从 TransformComponent 同步到 AudioEngine");
        ImGui::TextDisabled("(只读：全局唯一监听器)");
        ImGui::PopID();
    }

    // =====================================================================
    //  Add Component 菜单
    // =====================================================================

    void InspectorPanel::render_add_component_menu(GameObject& go)
    {
        if(ImGui::Button("+ Add Component"))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if(!ImGui::BeginPopup("AddComponentPopup"))
        {
            return;
        }

        // 已存在的组件类型置灰（s_allow_multiple=false 的类型只允许挂载一个）
        const bool has_transform = go.has_component<TransformComponent>();
        const bool has_mesh     = go.has_component<MeshRendererComponent>();
        const bool has_light    = go.has_component<LightComponent>();
        const bool has_rigid    = go.has_component<RigidBodyComponent>();
        const bool has_listener = go.has_component<AudioListenerComponent>();

        if(ImGui::MenuItem("Transform", nullptr, false, !has_transform))
        {
            go.add_component<TransformComponent>().make_active();
        }
        if(ImGui::MenuItem("MeshRenderer", nullptr, false, !has_mesh))
        {
            // 默认 Model 无效，make_active 会被拒绝；用户配置 Mesh/Material 后勾选 Active 即可
            go.add_component<MeshRendererComponent>().make_active();
        }
        if(ImGui::MenuItem("Light", nullptr, false, !has_light))
        {
            go.add_component<LightComponent>().make_active();
        }
        if(ImGui::MenuItem("RigidBody", nullptr, false, !has_rigid))
        {
            go.add_component<RigidBodyComponent>().make_active();
        }
        if(ImGui::MenuItem("AudioSource", nullptr, false, true))  // 允许多个
        {
            go.add_component<AudioSourceComponent>().make_active();
        }
        if(ImGui::MenuItem("AudioListener", nullptr, false, !has_listener))
        {
            go.add_component<AudioListenerComponent>().make_active();
        }

        ImGui::EndPopup();
    }
} // namespace ID
