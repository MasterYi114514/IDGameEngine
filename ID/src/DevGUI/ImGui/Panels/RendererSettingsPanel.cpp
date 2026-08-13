#include "DevGUI/ImGui/Panels/RendererSettingsPanel.hpp"

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

        end_window();
    }
} // namespace ID
