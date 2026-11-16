#pragma once

#include <cstdint>
#include <type_traits>

/**
 *  渲染后端 tag：决定窗口创建时建立哪种图形上下文。
 *
 *  不变量：
 *  1. 它是 `WindowDesc` 的唯一 tag，取值在创建前决定、运行期不切换。
 *  2. 只有 `Window::open_platform` 读这个 tag 与 `WindowDesc` 的描述分支；
 *     `WindowPool` 只原样透传描述符。
 */

namespace ID::Window
{
    enum class RenderApi : std::uint8_t
    {
        None   = 0,     // 不建图形上下文（Vulkan / CPU 客户端）
        OpenGL = 1,     // 按 OpenGL 描述建 GL 上下文
        Vulkan = 2,     // 不建 GL 上下文，只供 WSI 借用原生窗口
    };

    static_assert(sizeof(RenderApi) == sizeof(std::uint8_t), "RenderApi 必须是单字节枚举，否则描述符布局与跨模块传递失效");
} // namespace ID::Window
