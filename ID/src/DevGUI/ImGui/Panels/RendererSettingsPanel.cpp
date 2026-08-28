#include "DevGUI/ImGui/Panels/RendererSettingsPanel.hpp"

#include <fstream>

#include "Renderer/Render/Renderer.hpp"
#include "Log/Log.hpp"

namespace ID
{
    RendererSettingsPanel::RendererSettingsPanel() : ImGuiPanel("Renderer Settings", true) { }

    void RendererSettingsPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        ImGui::Text("Visual Pipeline:");
        ImGui::Separator();

        bool changed = false;

        changed |= ImGui::Checkbox("Shadow Mapping", &m_shadow_mapping);
        changed |= ImGui::Checkbox("Skybox", &m_skybox);
        changed |= ImGui::Checkbox("Post Processing", &m_post_processing);

        // 每次变更重建 RenderGraph
        if(changed)
        {
            Renderer::set_visual_pipeline(m_shadow_mapping, m_skybox, m_post_processing);
            ID_INFO("[RendererSettings] pipeline 更新: shadow={} skybox={} post_process={}",
                m_shadow_mapping, m_skybox, m_post_processing);
        }

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
