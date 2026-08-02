#pragma once

#include "IDWindowCore.hpp"
#include <cstdint>

namespace ID
{
    struct WindowProps;

    using WindowIDType = std::uint8_t;

    /*
    *   WindowID
    *   - 有效范围是 0 ~ 126, 127 是无效 ID
    *   - move-only：禁止拷贝，允许移动
    *   - 仅由 WindowPool 构造，外部只读
    */
    class IDWINDOW_API WindowID
    {
        friend WindowID create_window_id(WindowIDType id);
    public:
        WindowID() : m_ID(127) { }

        ~WindowID() = default;

        // 支持拷贝
        WindowID(const WindowID&) = default;
        WindowID& operator=(const WindowID&) = default;

        // 支持移动
        WindowID(WindowID&& other) noexcept : m_ID(other.m_ID)
        {
            other.m_ID = 127;    // 将被移动对象的 ID 设置为无效
        }

        WindowID& operator=(WindowID&& other) noexcept
        {
            if(this != &other)
            {
                m_ID = other.m_ID;
                other.m_ID = 127;
            }
            return *this;
        }

        /*
        *   获取窗口 ID 值。
        *   若 ID 已失效（窗口已销毁），返回 127 并报错。
        */
        WindowIDType get_id() const;

        /*
        *   是否为有效 ID。
        */
        bool is_valid() const { return m_ID != 127; }

        // 提供转换为 int 的操作符，方便调试和日志输出
        operator int() const { return static_cast<int>(m_ID); }

    private:
        // 私有构造函数，确保只能通过友元函数创建实例
        WindowID(WindowIDType id) : m_ID(id) { }

        WindowIDType m_ID = 127;    // 实际的窗口 ID，默认无效
    };
} // namespace ID
