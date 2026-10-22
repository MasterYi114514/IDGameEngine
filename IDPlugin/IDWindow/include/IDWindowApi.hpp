#pragma once

#include <cstdint>
#include <type_traits>

#include "Kernel/CoreTypes.hpp"
#include "Kernel/Plugin.hpp"

#include "Common/templates/IDHandle.hpp"

// 逐插件导出头由组装生成器产出：STATIC 归并时宏为空，Windows SHARED 时只有本插件自己的
// build 宏（PRIVATE，不向下传播）能打开 dllexport
// #include "IDWindowExport.generated.hpp"

namespace ID::Window
{
    enum class RenderAPI : uint8_t      // 暂时只支持 OpenGL 和 Vulkan
    {
        OpenGL,
        Vulkan
    };

    struct WindowProp
    {
        const char*     title;
        uint32_t        width;
        uint32_t        height;
        RenderAPI       render_api;
    };

    class WindowPool;
    class Window;
    using WindowHandle = ID::IDHandle<uint32_t, WindowPool>;

    WindowHandle(*create_window)(const WindowProp& prop) = nullptr;
    Window*(*get_window)(WindowHandle handle) = nullptr;

} // namespace ID::Window
