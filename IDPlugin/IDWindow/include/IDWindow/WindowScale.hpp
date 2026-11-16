#pragma once

#include <type_traits>

/**
 *  窗口内容缩放 POD：平台上报的 DPI 缩放系数，1.0 表示不做缩放。
 *
 *  不变量：
 *  1. 纯数据载体：只有两个 32 位浮点字段，无构造函数、无成员函数、可平凡复制。
 *  2. 无填充：`sizeof` 等于两个字段宽度之和。
 */

namespace ID::Window
{
    struct WindowScale
    {
        float x = 1.0f;      // 内容缩放 X
        float y = 1.0f;
    };

    static_assert(std::is_trivially_copyable_v<WindowScale>, "WindowScale 必须可平凡复制，否则不能跨模块按值传递");
    static_assert(std::is_standard_layout_v<WindowScale>, "WindowScale 必须标准布局，否则字段顺序与布局契约失效");
    static_assert(sizeof(WindowScale) == 2 * sizeof(float), "WindowScale 必须是两个 32 位浮点字段且无填充");
} // namespace ID::Window
