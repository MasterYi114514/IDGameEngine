#pragma once

#include <cstdint>

#include "IDWindow/OpenGLContextDesc.hpp"

/**
 *  GL 上下文的查询结果：与 `OpenGLContextDesc`（请求）成对，承载平台侧实际值。
 *
 *  不变量：
 *  1. 只描述 GL 上下文查询结果，不承载窗口状态（窗口状态是 `WindowStatus`）。
 *  2. `profile` 复用 `OpenGLContextDesc.hpp` 的 `GLProfile`，本头不另立同义枚举。
 *  3. 描述符允许尾部填充，不写 `sizeof` 断言。
 *  4. 只有该窗口的上下文归调用线程 current 时 `Window::get_context_info` 才填充本结构，
 *     否则返回本头的默认值并写日志；`is_debug` 与实际值、请求值三者的差异只记录、不构成失败。
 */

namespace ID::Window
{
    struct ContextInfo
    {
        std::uint32_t major    = 0;                      // GL 实际查询值，不是请求值
        std::uint32_t minor    = 0;
        GLProfile     profile  = GLProfile::Core;
        bool          is_debug = false;
    };
} // namespace ID::Window
