#pragma once

#include "IDpch.hpp"
#include "imgui.h"

namespace ID
{
    /**
     *  ImGuiPanel — 调试面板基类
     *
     *  每个功能面板封装为独立类，继承本基类并实现 on_imgui_render()。
     *  Panel 通过引用访问引擎系统，不持有所有权；修改引擎状态一律调用公开 API。
     */
    class ID_API ImGuiPanel
    {
    public:
        ImGuiPanel(const std::string& title, bool default_open = true);
        virtual ~ImGuiPanel() = default;

        // 每帧调用，绘制本 Panel 的 ImGui 控件
        virtual void on_imgui_render() = 0;

        const std::string& get_title() const { return m_title; }
        bool  is_open()    const { return m_open; }
        void  set_open(bool open) { m_open = open; }
        void  toggle_open()       { m_open = !m_open; }

    protected:
        std::string m_title;
        bool        m_open;

        // 辅助：开始/结束一个标准 ImGui 窗口
        bool begin_window(ImGuiWindowFlags flags = 0);
        void end_window();

    private:
        // 无 docking 分支下的简易默认布局：首次显示时的窗口位置
        float m_default_pos_x = 40.0f;
        float m_default_pos_y = 40.0f;
    };
} // namespace ID
