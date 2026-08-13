#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    ImGuiPanel::ImGuiPanel(const std::string& title, bool default_open)
        : m_title(title), m_open(default_open)
    {
        // 简易默认布局：各 Panel 首次显示时依次向右下偏移，避免窗口完全重叠
        // （后续版本接入 ImGui docking 分支后可移除）
        static float s_next_x = 40.0f;
        static float s_next_y = 40.0f;

        m_default_pos_x = s_next_x;
        m_default_pos_y = s_next_y;
        s_next_x += 36.0f;
        s_next_y += 36.0f;
    }

    bool ImGuiPanel::begin_window(ImGuiWindowFlags flags)
    {
        if(!m_open) return false;

        // 仅在首次显示时应用默认位置，之后用户可自由拖拽
        ImGui::SetNextWindowPos(ImVec2(m_default_pos_x, m_default_pos_y), ImGuiCond_FirstUseEver);

        if(!ImGui::Begin(m_title.c_str(), &m_open, flags))
        {
            ImGui::End();
            return false;
        }
        return true;
    }

    void ImGuiPanel::end_window()
    {
        ImGui::End();
    }
} // namespace ID
