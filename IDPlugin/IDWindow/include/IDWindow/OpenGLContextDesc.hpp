#pragma once

#include <cstdint>

#include "IDWindow/WindowHandle.hpp"

/**
 *  OpenGL 上下文请求描述与它的查询结果。
 *
 *  分级（由 `Window::open_platform` 消费，消费方不需判断）：
 *  - 硬约束（不满足即创建失败）：`major`/`minor` 最低版本、`profile`、`is_forward_compatible`、
 *    `is_double_buffered`。
 *  - 请求偏好（不构成拒绝理由）：`is_debug`；实际是否生效以 `ContextInfo::is_debug` 为准。
 *  - softPreferences（不承诺匹配）：颜色/深度/模板位宽、MSAA 采样数、sRGB。
 *
 *  不变量：两个结构都是描述符（纯数据 + NSDMI，无构造函数、无成员函数），允许尾部填充，
 *  因此不写 `sizeof` 断言；`FramebufferPreferences` 与 `GLProfile` 随本头落位，`ContextInfo`
 *  复用 `GLProfile`，两者都不包含 `Window.hpp`。
 *
 *  `share_with` 有效的窗口在创建期要求共享源**已同步且未 current**（GLFW 只对 Win32 明写该限制，
 *  本插件统一收紧为跨平台前提）："已同步"由应用负责，插件诊断"未 current"，不满足则创建失败并写日志。
 */

namespace ID::Window
{
    enum class GLProfile : std::uint8_t
    {
        Core          = 0,
        Compatibility = 1,
    };

    struct FramebufferPreferences
    {
        std::uint32_t red_bits     = 8;
        std::uint32_t green_bits   = 8;
        std::uint32_t blue_bits    = 8;
        std::uint32_t alpha_bits   = 8;
        std::uint32_t depth_bits   = 24;
        std::uint32_t stencil_bits = 8;
        std::uint32_t samples      = 0;         // MSAA 采样数，0 表示不请求
        bool          is_srgb      = false;
    };

    struct OpenGLContextDesc
    {
        std::uint32_t           major                 = 3;                  // 硬约束：最低要求版本
        std::uint32_t           minor                 = 3;
        GLProfile               profile               = GLProfile::Core;    // 硬约束
        bool                    is_forward_compatible = false;              // 硬约束
        bool                    is_double_buffered    = true;               // 硬约束
        bool                    is_debug              = false;              // 请求偏好，不是硬约束
        FramebufferPreferences  framebuffer           = {};
        WindowHandle            share_with            = {};                 // 无效句柄表示不共享；有效句柄必须是仍存活的窗口
    };
} // namespace ID::Window
