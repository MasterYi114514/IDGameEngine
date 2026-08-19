#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"
#include "Renderer/Camera/Camera.hpp"

namespace ID
{
    /*
    *   CameraPanel — 摄像机设置面板
    *
    *   展示/编辑当前相机：
    *   - Position / Front / Up（Pose 编辑）
    *   - Projection 类型（Perspective / Orthographic）与参数编辑
    *
    *   相机指针由外部注入（Sandbox 集成时传入 CameraLayer 的相机），
    *   不持有所有权。为空时面板显示提示。
    */
    class ID_API CameraPanel : public ImGuiPanel
    {
    public:
        CameraPanel();
        void on_imgui_render() override;

        // 注入要编辑的相机
        void set_camera(Camera* camera) { m_camera = camera; }

    private:
        void render_pose_editor(Camera& camera);
        void render_projection_editor(Camera& camera);

    private:
        Camera* m_camera = nullptr;
    };
} // namespace ID
