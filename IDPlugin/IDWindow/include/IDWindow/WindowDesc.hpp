#pragma once

#include <type_traits>

#include "IDWindow/OpenGLContextDesc.hpp"
#include "IDWindow/RenderApi.hpp"
#include "IDWindow/VulkanContextDesc.hpp"
#include "IDWindow/WindowSize.hpp"

/**
 *  窗口创建描述：`render_api` tag + 渲染描述联合体。
 *
 *  形态：**描述符**（`skills/cpp-style/references/design-patterns.md` 第 8 节）——纯数据载体，
 *  字段全公开 + NSDMI，无自定义构造函数、无成员函数、无虚函数；构造一律走聚合初始化。
 *  描述符只在 `WindowPool::create_window` 入口按 `const WindowDesc&` 读一次，不逐字节搬运、不进容器、
 *  没有字段指针表，因此 **tag 不要求位于偏移 0**，也不写 `offsetof` 断言（同文件第 5 节的
 *  "tag 在偏移 0"只适用于 tagged union 载体，本类型不是）。
 *
 *  不变量：
 *  1. `render_api` 是唯一的 tag：`OpenGL` 读 `render_desc.opengl`，`Vulkan` 读 `render_desc.vulkan`，
 *     `None` 两个分支都不读；读未写分支属调用方违约。
 *  2. `Window::open_platform` 是唯一读 tag 与 union 的地方；`WindowPool` 只原样透传本描述符。
 *  3. 描述符允许尾部填充，只钉可平凡复制与标准布局，**不钉 `sizeof`**。
 *  4. `vsync` 不在创建期描述里（运行期用 `Window::set_vsync`）；隐藏由 `is_visible` 表达；本版不做全屏。
 */

namespace ID::Window
{
    struct WindowDesc
    {
        const char* title        = "IDWindow";
        WindowSize  size         = { 1280, 720 };
        bool        is_resizable = true;
        bool        is_visible   = true;
        bool        is_decorated = true;

        RenderApi   render_api   = RenderApi::None;

        union RenderDesc
        {
            OpenGLContextDesc opengl{};     // 默认分支；带 NSDMI 才使 union 的默认构造不被删除
            VulkanContextDesc vulkan;
        } render_desc{};                    // 默认初始化第一个分支（opengl）
    };

    static_assert(std::is_trivially_copyable_v<WindowDesc>, "WindowDesc 必须可平凡复制，否则不能按值传递");
    static_assert(std::is_standard_layout_v<WindowDesc>, "WindowDesc 必须标准布局；描述符不做 sizeof 断言，允许尾部填充");
} // namespace ID::Window
