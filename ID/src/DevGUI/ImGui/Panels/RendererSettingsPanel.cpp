#include "DevGUI/ImGui/Panels/RendererSettingsPanel.hpp"

#include <fstream>
#include <typeindex>

#include "Application/Application.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/Renderer.hpp"
#include "Renderer/Render/RendererSettings.hpp"
#include "Renderer/Render/RenderPass/ShadowPass.hpp"
#include "Log/Log.hpp"

namespace
{
    /*
    *   compute_display_size：与 ViewportPanel 相同的缩放模式——
    *   在内容区域内保持 FBO 宽高比的最大显示矩形（G-Buffer 预览用）
    */
    ImVec2 compute_display_size(uint32_t fb_width, uint32_t fb_height)
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if(fb_width == 0 || fb_height == 0 || avail.x <= 0.0f || avail.y <= 0.0f)
        {
            return avail;
        }

        const float aspect = static_cast<float>(fb_width) / static_cast<float>(fb_height);

        float w = avail.x;
        float h = w / aspect;
        if(h > avail.y)
        {
            h = avail.y;
            w = h * aspect;
        }

        return ImVec2(w, h);
    }
} // 匿名命名空间

namespace ID
{
    RendererSettingsPanel::RendererSettingsPanel() : ImGuiPanel("Renderer Settings", true)
    {
        // ghost 节点列表 = 可调控 Pass 元数据（name → key；ForwardPass 常开不可关，不在列表）
        std::vector<RenderGraphEditorWidget::GhostNode> ghosts;
        ghosts.push_back({ "ShadowPass",      "shadow",       "阴影贴图 Pass（Shadow Mapping 开关）" });
        ghosts.push_back({ "SkyboxPass",      "skybox",       "天空盒 Pass（Skybox 开关）" });
        ghosts.push_back({ "TransparentPass", "skybox",       "透明批次 Pass（有 Skybox 时才装配，随 Skybox 开关）" });
        ghosts.push_back({ "PostProcessPass", "post_process", "后处理 Pass（Post Processing 开关）" });
        m_graph_editor.set_ghost_nodes(std::move(ghosts));

        // 节点按钮回调：key → 对应开关状态 → 统一装配（与 checkbox 同一状态源）
        m_graph_editor.set_toggle_callback([this](const std::string& key, bool enable)
        {
            if(key == "shadow")
            {
                m_shadow_mapping = enable;
            }
            else if(key == "skybox")
            {
                m_skybox = enable;
            }
            else if(key == "post_process")
            {
                m_post_processing = enable;
            }
            else
            {
                ID_WARN("[RendererSettings] 未知的管线开关 key: {}", key);
                return;
            }
            apply_pipeline();
        });
    }

    void RendererSettingsPanel::apply_pipeline()
    {
        Renderer::set_visual_pipeline(m_shadow_mapping, m_skybox, m_post_processing);
        ID_INFO("[RendererSettings] pipeline 更新: shadow={} skybox={} post_process={}",
            m_shadow_mapping, m_skybox, m_post_processing);
    }

    void RendererSettingsPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        ImGui::Text("Visual Pipeline:");
        ImGui::Separator();

        // Render Path 切换（Forward / Deferred）：切换后按当前三开关状态重装配
        {
            const bool is_deferred = (Renderer::get_render_path() == Renderer::RenderPath::Deferred);
            bool path_changed = false;
            path_changed |= ImGui::RadioButton("Forward", !is_deferred);
            ImGui::SameLine();
            path_changed |= ImGui::RadioButton("Deferred", is_deferred);
            if(path_changed)
            {
                Renderer::set_render_path(is_deferred ? Renderer::RenderPath::Forward : Renderer::RenderPath::Deferred);
            }
        }

        // 顶部开关：Shadow Mapping 在前（其参数组紧随其后显示），Skybox/Post Processing 在后
        // 开关与节点图按钮共用同一状态源，变更统一走 apply_pipeline
        bool changed = false;
        changed |= ImGui::Checkbox("Shadow Mapping", &m_shadow_mapping);
        if(changed)
        {
            apply_pipeline();
        }

        // Shadow 参数组（从属于 Shadow Mapping：勾选才展开，缩进显示层级）
        if(m_shadow_mapping)
        {
            ImGui::Indent();

            ImGui::Separator();

            // Shadow Quality：Low/Medium/High/Ultra Combo → 直连 ShadowPass 相机配置
            // （ShadowPass 每帧 ensure_resources 按尺寸重建贴图，即改即生效）
            ImGui::Text("Shadow Quality:");
            {
                if(ShadowPass* shadow_pass = static_cast<ShadowPass*>(
                       Renderer::get_render_graph().find_pass_by_type(std::type_index(typeid(ShadowPass)))))
                {
                    ShadowQuality& quality = shadow_pass->get_camera().get_config().param.quality;
                    const char* quality_names[] = { "Low (512)", "Medium (1024)", "High (2048)", "Ultra (4096)" };
                    int current_quality = static_cast<int>(quality);   // Combo 需可写指针（回写选择）
                    ImGui::SetNextItemWidth(360.0f);
                    if(ImGui::Combo("Quality", &current_quality, quality_names, 4))
                    {
                        quality = static_cast<ShadowQuality>(current_quality);
                    }
                    ImGui::TextDisabled("PCF radius: %u  (0=1x1, 1=3x3, 2=5x5, 3=7x7)",
                        shadow_quality_to_pcf_kernel_size(quality));
                }
            }

            ImGui::Separator();

            // Shadow Bias（ShadowPass → ShadowCamera 配置，即改即生效；ShadowPass 每帧读取）
            // 管线重装配（clear + 重建）会更换 Pass 实例 → 每帧 find_pass_by_type，不缓存指针
            ImGui::Text("Shadow Bias:");
            {
                if(ShadowPass* shadow_pass = static_cast<ShadowPass*>(
                       Renderer::get_render_graph().find_pass_by_type(std::type_index(typeid(ShadowPass)))))
                {
                    ShadowParam& param = shadow_pass->get_camera().get_config().param;
                    ImGui::SetNextItemWidth(360.0f);
                    ImGui::DragFloat("Depth Bias", &param.bias, 0.0001f, 0.0f, 0.01f, "%.5f");
                    ImGui::SetNextItemWidth(360.0f);
                    ImGui::DragFloat("Normal Bias", &param.normal_bias, 0.001f, 0.0f, 0.5f, "%.4f");
                    ImGui::TextDisabled("effective bias = Depth Bias x clamp(1/N.L, 1, 10) (slope-scaled)");
                }
            }

            ImGui::Separator();

            // Shadow Filtering：直接读写 RendererSettings（无面板本地副本，即改即生效）
            ImGui::Text("Shadow Filtering:");
            {
                RendererSettings& settings = get_renderer_settings();
                const int current_filter = static_cast<int>(settings.shadow_filter);
                if(ImGui::RadioButton("Hard", current_filter == 0))
                {
                    settings.shadow_filter = ShadowFilter::Hard;
                }
                ImGui::SameLine();
                if(ImGui::RadioButton("PCF", current_filter == 1))
                {
                    settings.shadow_filter = ShadowFilter::PCF;
                }
                ImGui::SameLine();
                if(ImGui::RadioButton("PCSS", current_filter == 2))
                {
                    settings.shadow_filter = ShadowFilter::PCSS;
                }

                // 面光源尺寸（仅 PCSS 模式显示）
                if(current_filter == 2)
                {
                    ImGui::SetNextItemWidth(360.0f);
                    ImGui::DragFloat("Light Size", &settings.light_size, 0.1f, 1.0f, 50.0f);
                }
            }

            ImGui::Separator();

            // Cascaded Shadow Maps (CSM)：Enable 4 层 / 关闭 1 层 + PSSM 分割参数（直接写 RendererSettings）
            ImGui::Text("Cascaded Shadow Maps (CSM):");
            {
                RendererSettings& settings = get_renderer_settings();
                bool csm_enabled = (settings.cascade_count > 1);
                if(ImGui::Checkbox("Enable CSM", &csm_enabled))
                {
                    settings.cascade_count = csm_enabled ? 4 : 1;
                }
                ImGui::TextDisabled("Current cascade count: %u", settings.cascade_count);

                ImGui::SetNextItemWidth(360.0f);
                ImGui::DragFloat("Cascade Lambda", &settings.cascade_lambda, 0.01f, 0.0f, 1.0f, "%.3f");
                ImGui::TextDisabled("0 = uniform split, 1 = logarithmic split");

                ImGui::SetNextItemWidth(360.0f);
                ImGui::DragFloat("Cascade Far Override", &settings.cascade_far_override, 1.0f, 0.0f, 1000.0f, "%.1f");
                ImGui::TextDisabled("0 = follow camera far, >0 = manual tighten");
            }

            ImGui::Unindent();
        }

        changed = false;
        changed |= ImGui::Checkbox("Skybox", &m_skybox);
        changed |= ImGui::Checkbox("Post Processing", &m_post_processing);
        if(changed)
        {
            apply_pipeline();
        }

        ImGui::Separator();

        // Lighting Model：直接读写 RendererSettings（无面板本地副本，即改即生效）
        ImGui::Text("Lighting Model:");
        {
            RendererSettings& settings = get_renderer_settings();
            const int current_model = static_cast<int>(settings.lighting_model);
            if(ImGui::RadioButton("Phong", current_model == 0))
            {
                settings.lighting_model = LightingModel::Phong;
            }
            ImGui::SameLine();
            if(ImGui::RadioButton("Blinn-Phong", current_model == 1))
            {
                settings.lighting_model = LightingModel::BlinnPhong;
            }
            ImGui::SameLine();
            if(ImGui::RadioButton("PBR", current_model == 2))
            {
                settings.lighting_model = LightingModel::PBR;
            }

            // PBR 全局参数（仅 PBR 模式显示，直接写 settings）
            if(current_model == 2)
            {
                ImGui::Indent();
                ImGui::Text("PBR Parameters:");
                ImGui::SetNextItemWidth(360.0f);
                ImGui::DragFloat("Metallic", &settings.metallic, 0.01f, 0.0f, 1.0f);
                ImGui::SetNextItemWidth(360.0f);
                ImGui::DragFloat("Roughness", &settings.roughness, 0.01f, 0.0f, 1.0f);
                ImGui::Unindent();
            }
        }

        ImGui::Separator();

        // G-Buffer 调试预览（仅延迟路径激活时显示）
        if(Renderer::get_render_path() == Renderer::RenderPath::Deferred)
        {
            if(ImGui::CollapsingHeader("G-Buffer Debug (Deferred)"))
            {
                const FrameBufferID gbuffer_fb = Renderer::get_gbuffer_fb();
                if(gbuffer_fb.is_valid())
                {
                    const uint32_t fb_width  = Application::get_instance().get_width();
                    const uint32_t fb_height = Application::get_instance().get_height();
                    const ImVec2 display = compute_display_size(fb_width, fb_height);

                    const char* labels[3] =
                    {
                        "RT0 Albedo + AmbientA",
                        "RT1 WorldPos + SpecA",
                        "RT2 Normal + ShinA"
                    };

                    for(int i = 0; i < 3; ++i)
                    {
                        const uint32_t texture = RenderCommand::get_framebuffer_color_texture(
                            gbuffer_fb, static_cast<uint32_t>(i));
                        ImGui::TextDisabled("%s", labels[i]);
                        if(texture != 0)
                        {
                            // UV 垂直翻转（与 Viewport 面板一致）
                            ImGui::Image(
                                reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture)),
                                display,
                                ImVec2(0.0f, 1.0f),
                                ImVec2(1.0f, 0.0f));
                        }
                        else
                        {
                            ImGui::TextDisabled("(附件无效)");
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("(G-Buffer 尚未创建)");
                }
            }
        }

        ImGui::Separator();

        // RenderGraph 节点画布：每帧注入最新快照（build_view 开销极小，pass 数 < 10）
        RGGraphView view;
        Renderer::get_render_graph().build_view(view);
        m_graph_editor.set_graph_view(view);
        const float canvas_height = std::max(ImGui::GetContentRegionAvail().y - 120.0f, 300.0f);
        m_graph_editor.render("RenderGraph Canvas", canvas_height);

        ImGui::Separator();
        ImGui::TextDisabled("Debug:");
        ImGui::Checkbox("Wireframe", &m_wireframe);
        ImGui::TextDisabled("(Wireframe / Normals / Colliders 调试渲染尚未实现)");

        // 导出 RenderGraph 管线结构（GraphViz .dot，可用 dot -Tpng render_graph.dot 渲染）
        if(ImGui::Button("Export RenderGraph (.dot)"))
        {
            std::string dot;
            if(Renderer::get_render_graph().export_graphviz(dot))
            {
                const std::string path = std::filesystem::absolute("render_graph.dot").string();
                std::ofstream file(path, std::ios::binary);
                if(file.is_open())
                {
                    file << dot;
                    ID_INFO("[RendererSettings] RenderGraph 管线结构已导出: {}", path);
                }
                else
                {
                    ID_ERROR("[RendererSettings] RenderGraph 导出失败（无法写入）: {}", path);
                }
            }
        }

        end_window();
    }
} // namespace ID
