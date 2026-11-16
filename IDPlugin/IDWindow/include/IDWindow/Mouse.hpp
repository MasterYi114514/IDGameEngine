#pragma once

#include <type_traits>

/**
 *  鼠标的几何载荷：光标位置与滚动量。
 *
 *  不变量：
 *  1. 都是纯数据载体（两个 `double` + NSDMI），无构造函数、无成员函数、可平凡复制、无填充。
 *  2. `Position` 是**相对窗口内容区左上角**的坐标系坐标，不是桌面绝对坐标，也不是 framebuffer 像素；
 *     `ScrollOffset` 是本次滚动事件的增量，不是累计值。
 */

namespace ID::Window::Mouse
{
    struct Position
    {
        double x = 0.0;      // 相对窗口内容区左上角的 X
        double y = 0.0;
    };

    static_assert(std::is_trivially_copyable_v<Position>, "Mouse::Position 必须可平凡复制，否则不能跨模块按值传递");
    static_assert(std::is_standard_layout_v<Position>, "Mouse::Position 必须标准布局，否则字段顺序与布局契约失效");
    static_assert(sizeof(Position) == 2 * sizeof(double), "Mouse::Position 必须是两个 double 且无填充");

    struct ScrollOffset
    {
        double x = 0.0;      // 水平滚动量
        double y = 0.0;      // 垂直滚动量
    };

    static_assert(std::is_trivially_copyable_v<ScrollOffset>, "Mouse::ScrollOffset 必须可平凡复制，否则不能跨模块按值传递");
    static_assert(std::is_standard_layout_v<ScrollOffset>, "Mouse::ScrollOffset 必须标准布局，否则字段顺序与布局契约失效");
    static_assert(sizeof(ScrollOffset) == 2 * sizeof(double), "Mouse::ScrollOffset 必须是两个 double 且无填充");
} // namespace ID::Window::Mouse
