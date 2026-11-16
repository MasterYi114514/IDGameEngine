#include "IDWindow/Window.hpp"

#include <array>
#include <cstdint>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>       // 平台头只在本文件出现：公开头不含任何平台头
#endif

#include "IDWindow.hpp"
#include "IDWindowLifeCycle.hpp"
#include "Log/Log.hpp"

/**
 *  原生借用与 Vulkan WSI 需求查询：本版只实现 Win32 后端。
 *
 *  不变量：
 *  1. 两者都是**只读借用**：不转移所有权、不创建任何图形 API 对象、不含 Vulkan 头、不出现 Vulkan 类型。
 *  2. `get_native_window_info` 同时写 `backend` 与对应分支；`NativeBackend::None` 或未知值不得读任何分支。
 *  3. `get_vulkan_wsi_requirements` 只对 `RenderApi::Vulkan` 窗口返回扩展表：GL 上下文窗口自己管理
 *     上下文，不需要实例扩展表。扩展名表是需求声明，不是保证。
 *  4. 平台类型（窗口句柄、实例句柄）只出现在本文件；公开头只有 `void*` 与插件自有类型。
 *  5. 失败诊断集中在 `ID_NOINLINE` + `ID_COLD` 冷函数里；查询本身不写日志、不分配。
 */
namespace ID::Window
{
    namespace
    {
        // ---- 失败冷路径：不内联、集中放置 ----

        ID_NOINLINE ID_COLD void report_uninitialized(const char* function_name) noexcept
        {
            Detail::set_error_text("窗口子系统尚未初始化");
            IDWINDOW_LOG_ERROR("{}: 窗口子系统尚未初始化，查询未执行", function_name);
        }

        ID_NOINLINE ID_COLD void report_platform_missing(const char* function_name) noexcept
        {
            Detail::set_error_text("平台窗口不存在：尚未创建或已销毁");
            IDWINDOW_LOG_ERROR("{}: 平台窗口不存在（尚未创建或已销毁），查询未执行", function_name);
        }

        ID_NOINLINE ID_COLD void report_native_handle_unavailable(const char* function_name) noexcept
        {
            Detail::set_error_text("平台没有返回原生窗口句柄");
            IDWINDOW_LOG_ERROR("{}: 平台没有返回原生窗口句柄，返回全零结果", function_name);
        }

        ID_NOINLINE ID_COLD void report_wsi_not_vulkan_window(const char* function_name) noexcept
        {
            Detail::set_error_text("该窗口不是 Vulkan 窗口：GL 上下文窗口不需要实例扩展表");
            IDWINDOW_LOG_WARN("{}: 该窗口不是 Vulkan 窗口，不需要实例扩展表，返回空表", function_name);
        }

        #if defined(_WIN32)
            // Win32 surface 所需的实例扩展名：需求声明，不是保证（renderer 必须自己枚举 loader 实际支持项）
            constexpr std::array<const char*, 2> kWsiWin32Extensions{ "VK_KHR_surface", "VK_KHR_win32_surface" };
        #else
            // 本版只实现 Win32：其它后端的原生借用与 WSI 需求查询一律返回全零结果
            ID_NOINLINE ID_COLD void report_backend_unsupported(const char* function_name) noexcept
            {
                Detail::set_error_text("本版只实现 Win32 后端：当前后端不支持原生借用与 WSI 需求查询");
                IDWINDOW_LOG_WARN("{}: 本版只实现 Win32 后端，当前后端返回全零结果", function_name);
            }
        #endif
    } // namespace

    NativeWindowInfo Window::get_native_window_info() const noexcept
    {
        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::get_native_window_info");
            return NativeWindowInfo{};
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_native_window_info");
            return NativeWindowInfo{};
        }

        #if defined(_WIN32)
            HWND const window_handle = glfwGetWin32Window(static_cast<GLFWwindow*>(m_native_window));
            if (window_handle == nullptr)
            {
                report_native_handle_unavailable("Window::get_native_window_info");
                return NativeWindowInfo{};
            }

            NativeWindowInfo info;
            info.backend         = NativeBackend::Win32;
            info.win32.hwnd      = static_cast<void*>(window_handle);
            info.win32.hinstance = reinterpret_cast<void*>(
                static_cast<std::intptr_t>(GetWindowLongPtrW(window_handle, GWLP_HINSTANCE)));
            return info;
        #else
            report_backend_unsupported("Window::get_native_window_info");
            return NativeWindowInfo{};
        #endif
    }

    VulkanWsiRequirements Window::get_vulkan_wsi_requirements() const noexcept
    {
        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::get_vulkan_wsi_requirements");
            return VulkanWsiRequirements{};
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_vulkan_wsi_requirements");
            return VulkanWsiRequirements{};
        }

        // 只有 Vulkan 窗口需要实例扩展表：tag 是创建期决定，运行期不变
        if (m_desc.render_api != RenderApi::Vulkan)
        {
            report_wsi_not_vulkan_window("Window::get_vulkan_wsi_requirements");
            return VulkanWsiRequirements{};
        }

        #if defined(_WIN32)
            VulkanWsiRequirements requirements;
            requirements.backend         = NativeBackend::Win32;
            requirements.extension_names = kWsiWin32Extensions.data();
            requirements.extension_count = static_cast<std::uint32_t>(kWsiWin32Extensions.size());
            return requirements;
        #else
            report_backend_unsupported("Window::get_vulkan_wsi_requirements");
            return VulkanWsiRequirements{};
        #endif
    }
} // namespace ID::Window
