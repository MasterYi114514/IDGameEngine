#include "DevGUI/ImGui/Panels/ViewportPanel.hpp"

#include "Application/Application.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/Renderer.hpp"

namespace ID
{
    ViewportPanel::ViewportPanel() : ImGuiPanel("Viewport", true) { }

    void ViewportPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        const FrameBufferID viewport_fb = Renderer::get_viewport_fb();
        const uint32_t texture = RenderCommand::get_framebuffer_color_texture(viewport_fb);

        if(texture == 0)
        {
            ImGui::TextDisabled("场景尚未渲染（无有效帧缓冲）");
            end_window();
            return;
        }

        // 渲染分辨率与窗口一致（RenderLayer 以窗口尺寸调用 Renderer::render）
        const uint32_t fb_width  = Application::get_instance().get_width();
        const uint32_t fb_height = Application::get_instance().get_height();

        // 在内容区域内保持 FBO 宽高比的最大显示矩形
        const ImVec2 display = compute_display_size(fb_width, fb_height);

        // ImGui::Image 显示 FBO 颜色纹理
        // UV 垂直翻转：GL 纹理坐标 v=0 在图像底部，ImGui 期望 v=0 在顶部
        ImGui::Image(
            reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture)),
            display,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));

        ImGui::TextDisabled("渲染分辨率 %u x %u（跟随窗口）", fb_width, fb_height);

        end_window();
    }

    ImVec2 ViewportPanel::compute_display_size(uint32_t fb_width, uint32_t fb_height) const
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if(fb_width == 0 || fb_height == 0 || avail.x <= 0.0f || avail.y <= 0.0f)
        {
            return avail;
        }

        const float aspect = static_cast<float>(fb_width) / static_cast<float>(fb_height);

        // 先按宽度适配，若超出高度则按高度适配
        float w = avail.x;
        float h = w / aspect;
        if(h > avail.y)
        {
            h = avail.y;
            w = h * aspect;
        }

        return ImVec2(w, h);
    }
} // namespace ID
