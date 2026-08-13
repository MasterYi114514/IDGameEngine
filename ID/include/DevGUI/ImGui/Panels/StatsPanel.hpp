#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    class Scene;

    /*
    *   StatsPanel — 渲染/性能统计面板
    *
    *   - FPS / 帧时间（带 60 帧历史曲线）
    *   - Renderer 统计（DrawCall / 三角形 / 光源 / 批次）
    *   - Scene 统计（GameObject 数量）
    */
    class ID_API StatsPanel : public ImGuiPanel
    {
    public:
        StatsPanel();
        void on_imgui_render() override;

    private:
        void render_frame_stats(float delta_time);
        void render_renderer_stats();
        void render_scene_stats(Scene* scene);

        // 平滑 FPS 历史（环形缓冲）
        static constexpr int FPS_HISTORY_SIZE = 60;
        float m_fps_history[FPS_HISTORY_SIZE] = {};
        int   m_fps_index = 0;
    };
} // namespace ID
