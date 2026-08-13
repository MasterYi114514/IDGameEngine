#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    /*
    *   ViewportPanel — 场景视口
    *
    *   在 ImGui 窗口中嵌入渲染结果：采样 Renderer 的\"最终显示 FBO\"
    *   （PostProcess 输出，RGBA8）并通过 ImGui::Image 显示。
    *
    *   数据流：
    *       Renderer::render() → scene_fb(HDR) → PostProcessPass → viewport_fb
    *           → blit 到默认 framebuffer（窗口显示）
    *           → ViewportPanel::ImGui::Image（面板显示）
    */
    class ID_API ViewportPanel : public ImGuiPanel
    {
    public:
        ViewportPanel();
        void on_imgui_render() override;

    private:
        // 计算保持宽高比的显示区域（返回实际绘制尺寸）
        ImVec2 compute_display_size(uint32_t fb_width, uint32_t fb_height) const;
    };
} // namespace ID
