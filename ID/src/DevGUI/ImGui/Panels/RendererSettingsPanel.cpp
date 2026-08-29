#include "DevGUI/ImGui/Panels/RendererSettingsPanel.hpp"

#include <fstream>

#include "Application/Application.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/Renderer.hpp"
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

        // 顶部开关与节点图按钮共用同一状态源，变更统一走 apply_pipeline
        bool changed = false;
        changed |= ImGui::Checkbox("Shadow Mapping", &m_shadow_mapping);
        changed |= ImGui::Checkbox("Skybox", &m_skybox);
        changed |= ImGui::Checkbox("Post Processing", &m_post_processing);
        if(changed)
        {
            apply_pipeline();
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
