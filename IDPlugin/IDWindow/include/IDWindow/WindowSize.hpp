#pragma once

#include <cstdint>
#include <type_traits>

/**
 *  窗口尺寸 POD：同一个类型既表示逻辑尺寸（窗口操作层面的尺寸）也表示帧缓冲像素尺寸，
 *  由调用接口区分。
 *
 *  不变量：
 *  1. 纯数据载体：只有两个 32 位无符号字段，无构造函数、无成员函数、可平凡复制。
 *  2. 无填充：`sizeof` 等于两个字段宽度之和。
 */

namespace ID::Window
{
    struct WindowSize
    {
        std::uint32_t width  = 0;    // 逻辑宽度或帧缓冲像素宽度，由接口区分
        std::uint32_t height = 0;
    };

    static_assert(std::is_trivially_copyable_v<WindowSize>, "WindowSize 必须可平凡复制，否则不能跨模块按值传递");
    static_assert(std::is_standard_layout_v<WindowSize>, "WindowSize 必须标准布局，否则字段顺序与布局契约失效");
    static_assert(sizeof(WindowSize) == 2 * sizeof(std::uint32_t), "WindowSize 必须是两个 32 位字段且无填充");
} // namespace ID::Window
