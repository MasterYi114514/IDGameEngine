#pragma once

#include <cstdint>

#include "IDWindow/NativeWindowInfo.hpp"

/**
 *  Vulkan WSI 对窗口的要求：后端标识与做 surface 所需的实例扩展名静态表。
 *
 *  不变量：
 *  1. 扩展名表是**需求声明**，不是保证：renderer 必须自己枚举 loader 实际支持项，缺项显式失败。
 *  2. `extension_names` 指向模块加载期的静态数据，有效期到本模块卸载为止；不含 Vulkan 类型。
 *  3. `backend` 复用 `NativeWindowInfo.hpp` 的 `NativeBackend`，本头不另立同义枚举。
 *  4. 本版只实现 Win32 的扩展表（`VK_KHR_surface`、`VK_KHR_win32_surface`）；X11/Wayland/Cocoa 的
 *     扩展表留待后续实现，当前一律返回空表。本头不含任何 Vulkan 头，也不出现任何 Vulkan 类型。
 */

namespace ID::Window
{
    struct VulkanWsiRequirements
    {
        NativeBackend       backend         = NativeBackend::None;
        const char* const*  extension_names = nullptr;      // 静态字符串表，有效期同本模块加载期
        std::uint32_t       extension_count = 0;
    };
} // namespace ID::Window
