#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

#include <deque>
#include <string>

namespace ID
{
    /*
    *   ConsolePanel — 日志控制台
    *
    *   收集引擎日志输出（通过 IDLog 的 sink 回调接入，见 Log::set_sink），
    *   支持按级别过滤、关键字搜索、清空与自动滚动。
    *
    *   注意：add_log 为静态方法（sink 回调入口），内部转发给全局实例
    *   s_instance（ConsolePanel 构造时注册、析构时注销）。
    */
    class ID_API ConsolePanel : public ImGuiPanel
    {
    public:
        ConsolePanel();
        ~ConsolePanel() override;

        void on_imgui_render() override;

        // 供 Log 系统 sink 回调调用（ID::Log::set_sink）
        static void add_log(const std::string& message, int level);

    private:
        struct LogEntry
        {
            std::string message;
            int         level;      // 0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error
            float       timestamp;  // 相对进程启动的秒数
        };

        void render_filters();
        void render_log_line(const LogEntry& entry);
        void push_log(const std::string& message, int level);

        // 环形缓冲区，避免无限增长
        static constexpr size_t MAX_LOGS = 1000;
        std::deque<LogEntry> m_logs;
        bool m_scroll_to_bottom = true;

        // 过滤状态
        bool m_show_trace = true;
        bool m_show_debug = true;
        bool m_show_info  = true;
        bool m_show_warn  = true;
        bool m_show_error = true;

        // 关键字搜索（ImGui InputText 需要 char 缓冲）
        char m_search_filter[128] = "";

        // 全局实例（sink 回调转发目标）
        static ConsolePanel* s_instance;
    };
} // namespace ID
