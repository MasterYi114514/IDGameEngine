#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"
#include "DevGUI/ImGui/Widgets/RenderGraphEditorWidget.hpp"

namespace ID
{
    /*
    *   RendererSettingsPanel — 渲染设置面板
    *
    *   控制 Visual Pipeline 的开关（阴影 / Skybox / 后处理），
    *   每次变更调用 Renderer::set_visual_pipeline() 重建 RenderGraph。
    *
    *   说明：引擎未提供 pipeline 状态查询接口，Panel 内部维护开关状态，
    *   初始值与 Sandbox RenderLayer 的默认配置一致（全部开启）。
    *   开关状态同时驱动 RenderGraphEditorWidget 节点图：checkbox 与节点按钮
    *   共用同一状态源，变更统一收敛到 apply_pipeline()（避免重复装配）。
    */
    class ID_API RendererSettingsPanel : public ImGuiPanel
    {
    public:
        RendererSettingsPanel();
        void on_imgui_render() override;

    private:
        void apply_pipeline();   // 统一装配入口：状态 → set_visual_pipeline + 日志

        bool m_shadow_mapping  = true;
        bool m_skybox          = true;
        bool m_post_processing = true;
        bool m_wireframe       = false;   // 本地状态（引擎暂未实现 wireframe 调试渲染）

        RenderGraphEditorWidget m_graph_editor;   // RenderGraph 节点图控件（值语义，构造于成员初始化）
    };
} // namespace ID
