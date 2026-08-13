#include "DevGUI/ImGui/Panels/ConsolePanel.hpp"

#include <chrono>
#include <algorithm>
#include <cctype>

namespace
{
    // 日志级别 → 显示名称（与设计书 0=Trace ... 4=Error 对应）
    constexpr const char* k_level_names[] = {
        "Trace", "Debug", "Info", "Warn", "Error"
    };

    // 日志级别 → 颜色
    const ImVec4 k_level_colors[] = {
        ImVec4(0.55f, 0.55f, 0.60f, 1.0f),  // Trace   灰
        ImVec4(0.45f, 0.70f, 0.85f, 1.0f),  // Debug   蓝灰
        ImVec4(0.80f, 0.85f, 0.90f, 1.0f),  // Info    白灰
        ImVec4(0.95f, 0.80f, 0.30f, 1.0f),  // Warn    黄
        ImVec4(0.95f, 0.35f, 0.35f, 1.0f),  // Error   红
    };

    // 级别过滤按钮（按下状态高亮，点击切换）
    bool level_toggle_button(const char* label, bool& value)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,
            value ? ImVec4(0.30f, 0.50f, 0.90f, 1.0f) : ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.95f, 1.0f));
        const bool clicked = ImGui::Button(label);
        ImGui::PopStyleColor(2);
        if(clicked)
        {
            value = !value;
        }
        return clicked;
    }

    // 将字符串转为小写（用于大小写不敏感的搜索）
    std::string to_lower_copy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }
} // 匿名命名空间

namespace ID
{
    ConsolePanel* ConsolePanel::s_instance = nullptr;

    ConsolePanel::ConsolePanel() : ImGuiPanel("Console", true)
    {
        s_instance = this;
    }

    ConsolePanel::~ConsolePanel()
    {
        if(s_instance == this)
        {
            s_instance = nullptr;
        }
    }

    void ConsolePanel::add_log(const std::string& message, int level)
    {
        if(!s_instance) return;
        s_instance->push_log(message, level);
    }

    void ConsolePanel::push_log(const std::string& message, int level)
    {
        static const auto s_start = std::chrono::steady_clock::now();
        const float seconds = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_start).count();

        m_logs.push_back(LogEntry{ message, level, seconds });

        // 环形缓冲：超出上限丢弃最旧的日志
        if(m_logs.size() > MAX_LOGS)
        {
            m_logs.pop_front();
        }
    }

    void ConsolePanel::on_imgui_render()
    {
        if(!begin_window()) return;

        render_filters();

        // ---- 日志滚动区 ----
        ImGui::BeginChild("LogScrollArea", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

        for(const LogEntry& entry : m_logs)
        {
            render_log_line(entry);
        }

        // 自动滚动到底部（仅当用户停留在底部时跟随）
        if(m_scroll_to_bottom
            && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        end_window();
    }

    void ConsolePanel::render_filters()
    {
        // 级别过滤按钮
        level_toggle_button("Trace", m_show_trace); ImGui::SameLine();
        level_toggle_button("Debug", m_show_debug); ImGui::SameLine();
        level_toggle_button("Info",  m_show_info);  ImGui::SameLine();
        level_toggle_button("Warn",  m_show_warn);  ImGui::SameLine();
        level_toggle_button("Error", m_show_error); ImGui::SameLine();

        // 搜索框
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputTextWithHint("##search", "Search...", m_search_filter, sizeof(m_search_filter));

        // 清空按钮
        ImGui::SameLine();
        if(ImGui::Button("Clear"))
        {
            m_logs.clear();
        }

        ImGui::Separator();
    }

    void ConsolePanel::render_log_line(const LogEntry& entry)
    {
        // 级别过滤
        const bool show = (entry.level == 0 && m_show_trace)
            || (entry.level == 1 && m_show_debug)
            || (entry.level == 2 && m_show_info)
            || (entry.level == 3 && m_show_warn)
            || (entry.level == 4 && m_show_error);
        if(!show) return;

        // 关键字过滤（大小写不敏感）
        if(m_search_filter[0] != '\0')
        {
            const std::string lower_message = to_lower_copy(entry.message);
            const std::string lower_filter  = to_lower_copy(m_search_filter);
            if(lower_message.find(lower_filter) == std::string::npos)
            {
                return;
            }
        }

        const char* level_name = k_level_names[entry.level];
        const ImVec4& color    = k_level_colors[entry.level];

        // [mm:ss.mmm] [LEVEL] message
        const int minutes = static_cast<int>(entry.timestamp) / 60;
        const float seconds = entry.timestamp - minutes * 60.0f;

        ImGui::TextColored(color, "[%02d:%06.3f] [%s] %s",
            minutes, seconds, level_name, entry.message.c_str());
    }
} // namespace ID
