#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    class Scene;

    /*
    *   PhysicsSettingsPanel — 物理设置面板
    *
    *   - 重力向量编辑（实时生效）
    *   - 固定时间步长 / 最大子步数展示（引擎当前使用默认值 1/60、4）
    *   - Pause Physics 开关（暂停/恢复当前场景）
    */
    class ID_API PhysicsSettingsPanel : public ImGuiPanel
    {
    public:
        PhysicsSettingsPanel();
        void on_imgui_render() override;

        // 设置目标场景（由 ImGuiLayer 每帧注入当前活跃场景）
        void set_context(Scene* scene) { m_context = scene; }

    private:
        Scene* m_context = nullptr;
    };
} // namespace ID
