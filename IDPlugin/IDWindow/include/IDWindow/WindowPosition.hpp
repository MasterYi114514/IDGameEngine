#pragma once

#include <cstdint>
#include <type_traits>

/**
 *  窗口位置 POD：屏幕坐标系下的窗口左上角位置，多显示器布局下可为负值。
 *
 *  不变量：
 *  1. 纯数据载体：只有两个 32 位有符号字段，无构造函数、无成员函数、可平凡复制。
 *  2. 无填充：`sizeof` 等于两个字段宽度之和。
 */

namespace ID::Window
{
    struct WindowPosition
    {
        std::int32_t x = 0;      // 屏幕坐标 X，多显示器布局下可为负
        std::int32_t y = 0;
    };

    static_assert(std::is_trivially_copyable_v<WindowPosition>, "WindowPosition 必须可平凡复制，否则不能跨模块按值传递");
    static_assert(std::is_standard_layout_v<WindowPosition>, "WindowPosition 必须标准布局，否则字段顺序与布局契约失效");
    static_assert(sizeof(WindowPosition) == 2 * sizeof(std::int32_t), "WindowPosition 必须是两个 32 位字段且无填充");
} // namespace ID::Window
