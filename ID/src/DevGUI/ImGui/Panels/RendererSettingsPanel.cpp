#include "DevGUI/ImGui/Panels/RendererSettingsPanel.hpp"

#include <fstream>

#include "Renderer/Render/Renderer.hpp"
#include "Log/Log.hpp"

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
