#include "DevGUI/ImGui/Panels/StatsPanel.hpp"

#include "Renderer/Render/Renderer.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

namespace ID
{
    StatsPanel::StatsPanel() : ImGuiPanel("Stats", true) { }

    void StatsPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        // ImGui 的 DeltaTime 即上一帧的实际帧时间
        const float delta_time = ImGui::GetIO().DeltaTime;

        render_frame_stats(delta_time);
        ImGui::Separator();
        render_renderer_stats();
        ImGui::Separator();
        render_scene_stats(&SceneManager::get_current_scene());

        end_window();
    }

    void StatsPanel::render_frame_stats(float delta_time)
    {
        const float fps = (delta_time > 0.0f) ? 1.0f / delta_time : 0.0f;

        // 记录 FPS 历史（环形）
        m_fps_history[m_fps_index] = fps;
        m_fps_index = (m_fps_index + 1) % FPS_HISTORY_SIZE;

        ImGui::Text("FPS:   %.1f", fps);
        ImGui::Text("Frame: %.2f ms", delta_time * 1000.0f);

        // 帧率曲线（环形缓冲：values_offset 指向最旧数据）
        ImGui::PlotLines("##fps_curve", m_fps_history, FPS_HISTORY_SIZE,
            m_fps_index, nullptr, 0.0f, 120.0f, ImVec2(0.0f, 60.0f));
    }

    void StatsPanel::render_renderer_stats()
    {
        const RendererStatistics& stats = Renderer::get_statistics();

        ImGui::Text("Draw Calls:  %u", stats.draw_calls);
        ImGui::Text("Triangles:   %u", stats.triangles);
        ImGui::Text("Lights:      %u", stats.lights);
        ImGui::Text("Opaque:      %u", stats.opaque);
        ImGui::Text("Transparent: %u", stats.transparent);
    }

    void StatsPanel::render_scene_stats(Scene* scene)
    {
        if(!scene)
        {
            ImGui::Text("Scene:  (none)");
            return;
        }

        ImGui::Text("Scene: %s", scene->get_name().c_str());
        ImGui::Text("GameObjects: %zu", scene->get_game_object_count());
    }
} // namespace ID
