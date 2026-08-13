#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    /*
    *   AudioSettingsPanel — 音频设置面板
    *
    *   - Master Volume 滑块（实时生效）
    *   - Listener 状态只读展示（引擎未提供查询接口，显示说明文字）
    */
    class ID_API AudioSettingsPanel : public ImGuiPanel
    {
    public:
        AudioSettingsPanel();
        void on_imgui_render() override;
    };
} // namespace ID
